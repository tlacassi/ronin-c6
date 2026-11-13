#include "ws_control.h"
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ronin_ble.h"
#include "ws_arbiter.h"
#include "ws_protocol.h"

/* Safety model (same guarantees as the HTTP routes):
 *  - Commands only come from the single controller socket, bound to a BLE link session by "hello";
 *    ronin_ble_* validates that session on every command.
 *  - Socket close (including LRU purge), read error, invalid frame, or a silent controller all
 *    call ronin_ble_stop() and cancel the zoom. The 400 ms motion watchdog and the 1.5 s zoom lease
 *    live in ronin_ble.c / zoom_motion.c and run regardless of socket state. */
#define STATE_PERIOD_MS 250u
#define STATE_BUFFER 900u
#define ERROR_MIN_INTERVAL_US 500000

static const char *TAG = "ronin_ws";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static ws_arbiter_t arbiter;
static httpd_handle_t server_handle = NULL;
static ws_status_fn status_fn = NULL;
static int64_t last_error_us = 0;

static void safe_stop(uint32_t session)
{
    ronin_ble_stop();
    (void)ronin_ble_zoom_stop(session);
}

void ws_control_on_close(httpd_handle_t server, int fd)
{
    (void)server;
    uint32_t session = 0u;
    portENTER_CRITICAL(&lock);
    bool was_controller = ws_arbiter_remove(&arbiter, fd, &session);
    portEXIT_CRITICAL(&lock);
    if(was_controller)
    {
        safe_stop(session);
        ESP_LOGW(TAG, "Controller socket closed; motion and zoom stopped");
    }
    close(fd);
}

static esp_err_t reply(httpd_req_t *req, const char *text, size_t length)
{
    if(length == 0u) { return ESP_OK; }
    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)text, .len = length};
    return httpd_ws_send_frame(req, &frame);
}

static void reply_error(httpd_req_t *req, ws_error_t code)
{
    int64_t now = esp_timer_get_time();
    if((code != WS_ERR_BAD) && ((now - last_error_us) < ERROR_MIN_INTERVAL_US)) { return; }
    last_error_us = now;
    char text[8];
    (void)reply(req, text, ws_format_error(text, sizeof(text), code));
}

/* Invalid frame or broken read: stop everything like the HTTP error paths, then drop the socket. */
static esp_err_t drop(httpd_req_t *req, int fd, bool notify)
{
    uint32_t session = 0u;
    portENTER_CRITICAL(&lock);
    bool controller = ws_arbiter_controller(&arbiter, fd, &session);
    portEXIT_CRITICAL(&lock);
    ronin_ble_stop();
    if(controller) { (void)ronin_ble_zoom_stop(session); }
    if(notify) { reply_error(req, WS_ERR_BAD); }
    return ESP_FAIL;
}

static esp_err_t handshake(httpd_req_t *req, int fd)
{
    char origin[80] = {0}, host[80] = {0};
    if((httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) ||
       (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) ||
       !ws_origin_allowed(origin, host))
    {
        /* The 101 reply is already sent by esp_http_server; closing here before any
           message is accepted gives a hijacking page nothing but an immediate close. */
        ESP_LOGW(TAG, "Rejected WebSocket with foreign or missing Origin");
        return ESP_FAIL;
    }
    portENTER_CRITICAL(&lock);
    bool added = ws_arbiter_add(&arbiter, fd, esp_timer_get_time());
    portEXIT_CRITICAL(&lock);
    if(!added)
    {
        char text[8];
        (void)reply(req, text, ws_format_error(text, sizeof(text), WS_ERR_BUSY));
        return ESP_FAIL;
    }
    return ESP_OK;
}

static ws_error_t refusal(uint32_t session, bool zoom)
{
    bool ready = false;
    uint32_t current = 0u;
    ronin_ble_link(&ready, &current);
    if(current != session) { return WS_ERR_SESSION; }
    return zoom ? WS_ERR_ZOOM : WS_ERR_LINK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if(req->method == HTTP_GET) { return handshake(req, fd); }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    if(httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) { return drop(req, fd, false); }
    if((frame.type != HTTPD_WS_TYPE_TEXT) || !frame.final || frame.fragmented ||
       (frame.len == 0u) || (frame.len > WS_MESSAGE_MAX))
    { return drop(req, fd, true); }
    uint8_t payload[WS_MESSAGE_MAX + 1u] = {0};
    frame.payload = payload;
    if(httpd_ws_recv_frame(req, &frame, frame.len) != ESP_OK) { return drop(req, fd, false); }
    ws_message_t message;
    if(!ws_parse((const char *)payload, frame.len, &message)) { return drop(req, fd, true); }

    int64_t now = esp_timer_get_time();
    uint32_t session = 0u;
    bool controller = false;
    int evicted = -1;
    ws_role_t role = WS_ROLE_UNKNOWN;
    portENTER_CRITICAL(&lock);
    ws_arbiter_touch(&arbiter, fd, now);
    if(message.type == WS_MSG_HELLO) { role = ws_arbiter_hello(&arbiter, fd, message.session, now, &evicted); }
    else { controller = ws_arbiter_controller(&arbiter, fd, &session); }
    portEXIT_CRITICAL(&lock);

    char text[16];
    switch(message.type)
    {
    case WS_MSG_PING:
        return reply(req, text, ws_format_pong(text, sizeof(text), message.timestamp));
    case WS_MSG_HELLO:
    {
        bool ready = false;
        uint32_t current = 0u;
        ronin_ble_link(&ready, &current);
        if(role == WS_ROLE_UNKNOWN) { return drop(req, fd, true); }
        if((role == WS_ROLE_CONTROLLER) && (current != message.session))
        {
            /* Stale session: give the slot back so a fresh hello can claim it. */
            portENTER_CRITICAL(&lock);
            uint32_t ignored = 0u;
            (void)ws_arbiter_remove(&arbiter, fd, &ignored);
            (void)ws_arbiter_add(&arbiter, fd, now);
            portEXIT_CRITICAL(&lock);
            reply_error(req, WS_ERR_SESSION);
            return ESP_OK;
        }
        if(evicted >= 0)
        {
            safe_stop(message.session);
            (void)httpd_sess_trigger_close(server_handle, evicted);
        }
        return reply(req, text, ws_format_role(text, sizeof(text), role == WS_ROLE_CONTROLLER));
    }
    default: break;
    }

    if(!controller) { reply_error(req, WS_ERR_ROLE); return ESP_OK; }
    bool accepted = false;
    bool zoom = false;
    switch(message.type)
    {
    case WS_MSG_MOVE: accepted = ronin_ble_move(message.yaw, message.pitch, session); break;
    case WS_MSG_ZOOM: zoom = true; accepted = ronin_ble_zoom_goto(session, message.zoom_position, message.zoom_speed); break;
    case WS_MSG_ZOOM_STOP: zoom = true; accepted = ronin_ble_zoom_stop(session); break;
    case WS_MSG_KEEPALIVE: zoom = true; accepted = ronin_ble_zoom_keepalive(session); break;
    default: return drop(req, fd, true);
    }
    if(!accepted) { reply_error(req, refusal(session, zoom)); }
    return ESP_OK;
}

static void push_task(void *arg)
{
    (void)arg;
    static char json[STATE_BUFFER];
    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(STATE_PERIOD_MS));
        int64_t now = esp_timer_get_time();
        ws_action_t actions[WS_MAX_CLIENTS];
        int fds[WS_MAX_CLIENTS];
        portENTER_CRITICAL(&lock);
        unsigned action_count = ws_arbiter_poll(&arbiter, now, actions, WS_MAX_CLIENTS);
        unsigned fd_count = ws_arbiter_fds(&arbiter, fds);
        portEXIT_CRITICAL(&lock);
        for(unsigned i = 0u; i < action_count; i++)
        {
            if(actions[i].kind == WS_ACTION_DEGRADE)
            {
                ESP_LOGW(TAG, "Controller heartbeat lost; motion and zoom stopped");
                safe_stop(actions[i].session);
            }
            else if(actions[i].kind == WS_ACTION_CLOSE)
            {
                ESP_LOGW(TAG, "WebSocket silent for too long; closing");
                (void)httpd_sess_trigger_close(server_handle, actions[i].fd);
            }
        }
        if(fd_count == 0u) { continue; }
        json[0] = 's';
        size_t length = status_fn(json + 1, sizeof(json) - 1u);
        if(length == 0u) { continue; }
        for(unsigned i = 0u; i < fd_count; i++)
        {
            if(httpd_ws_get_fd_info(server_handle, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET)
            {
                (void)httpd_sess_trigger_close(server_handle, fds[i]);
                continue;
            }
            httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)json, .len = length + 1u};
            if(httpd_ws_send_frame_async(server_handle, fds[i], &frame) != ESP_OK)
            { (void)httpd_sess_trigger_close(server_handle, fds[i]); }
        }
    }
}

esp_err_t ws_control_start(httpd_handle_t server, ws_status_fn status)
{
    server_handle = server;
    status_fn = status;
    portENTER_CRITICAL(&lock);
    ws_arbiter_init(&arbiter);
    portEXIT_CRITICAL(&lock);
    const httpd_uri_t uri = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
    esp_err_t err = httpd_register_uri_handler(server, &uri);
    if(err != ESP_OK) { return err; }
    return (xTaskCreate(push_task, "ws_push", 4096, NULL, 4, NULL) == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
