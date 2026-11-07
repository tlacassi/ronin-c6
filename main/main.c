#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "ronin_ble.h"
#include "motion_parse.h"
#include "cJSON.h"
#include "mdns.h"

static const char *TAG = "ronin";
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t gamepad_js_start[] asm("_binary_gamepad_js_start");
extern const uint8_t gamepad_js_end[] asm("_binary_gamepad_js_end");
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_START)) { esp_wifi_connect(); }
    else if((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED))
    {
        ronin_ble_invalidate();
        ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting");
        esp_wifi_connect();
    }
    else if((base == IP_EVENT) && (id == IP_EVENT_STA_GOT_IP))
    {
        ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Open http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}
static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)index_html_start, index_html_end - index_html_start - 1);
}
static esp_err_t gamepad_script(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)gamepad_js_start, gamepad_js_end - gamepad_js_start - 1);
}
static esp_err_t status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char json[128] = {0};
    uint8_t battery = 0u;
    uint32_t session = 0u;
    bool ready = false;
    ronin_ble_link(&ready, &session);
    if(ronin_ble_battery(&battery))
    { snprintf(json, sizeof(json), "{\"ble\":%s,\"battery\":%u,\"session\":%" PRIu32 "}", ready ? "true" : "false", (unsigned)battery, session); }
    else
    { snprintf(json, sizeof(json), "{\"ble\":%s,\"battery\":null,\"session\":%" PRIu32 "}", ready ? "true" : "false", session); }
    return httpd_resp_sendstr(req, json);
}
static bool read_session(httpd_req_t *req, uint32_t *session)
{
    char token[11] = {0};
    if(httpd_req_get_hdr_value_str(req, "X-Ronin-Session", token, sizeof(token)) != ESP_OK) { return false; }
    size_t length = strlen(token);
    if((length == 0u) || (length > 10u)) { return false; }
    uint64_t value = 0u;
    for(size_t i = 0u; i < length; i++)
    {
        if((token[i] < '0') || (token[i] > '9')) { return false; }
        value = value * 10u + (unsigned)(token[i] - '0');
        if(value > UINT32_MAX) { return false; }
    }
    *session = (uint32_t)value;
    return true;
}
static esp_err_t move(httpd_req_t *req)
{
    char body[48] = {0};
    uint32_t session = 0u;
    if(!read_session(req, &session))
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing session"); return ESP_FAIL; }
    if((req->content_len <= 0) || (req->content_len >= sizeof(body)))
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body"); return ESP_FAIL; }
    int received = 0;
    while(received < req->content_len)
    {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if(n <= 0) { ronin_ble_stop(); return ESP_FAIL; }
        received += n;
    }
    int16_t yaw = 0, pitch = 0;
    if(!motion_parse(body, (size_t)received, &yaw, &pitch))
    { ronin_ble_stop(); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid motion"); return ESP_FAIL; }
    if(!ronin_ble_move(yaw, pitch, session))
    { httpd_resp_set_status(req, "503 Service Unavailable"); return httpd_resp_sendstr(req, "BLE unavailable"); }
    return httpd_resp_sendstr(req, "OK");
}
static esp_err_t devices(httpd_req_t *req)
{
    char names[RONIN_SCAN_MAX][RONIN_NAME_MAX + 1u] = {{0}};
    char selected[RONIN_NAME_MAX + 1u] = {0};
    size_t count = ronin_ble_devices(names);
    ronin_ble_selected(selected);
    cJSON *root = cJSON_CreateObject();
    if(root == NULL) { return ESP_FAIL; }
    cJSON *list = cJSON_AddArrayToObject(root, "devices");
    if((list == NULL) || (cJSON_AddStringToObject(root, "selected", selected) == NULL))
    { cJSON_Delete(root); return ESP_FAIL; }
    for(size_t i = 0u; i < count; i++)
    {
        cJSON *name = cJSON_CreateString(names[i]);
        if(name == NULL) { cJSON_Delete(root); return ESP_FAIL; }
        cJSON_AddItemToArray(list, name);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if(json == NULL) { return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, json);
    free(json);
    return result;
}
static esp_err_t select_device(httpd_req_t *req)
{
    char name[RONIN_NAME_MAX + 1u] = {0};
    if((req->content_len <= 0) || (req->content_len > RONIN_NAME_MAX))
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid device name"); return ESP_FAIL; }
    int total = 0;
    while(total < req->content_len)
    {
        int n = httpd_req_recv(req, name + total, req->content_len - total);
        if(n <= 0) { return ESP_FAIL; }
        total += n;
    }
    if(memchr(name, '\0', (size_t)total) != NULL)
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid device name"); return ESP_FAIL; }
    if(!ronin_ble_select(name))
    { httpd_resp_set_status(req, "409 Conflict"); return httpd_resp_sendstr(req, "Not available; disconnect current Ronin first"); }
    return httpd_resp_sendstr(req, "OK");
}
static void start_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.recv_wait_timeout = 1;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    const httpd_uri_t page_uri = {.uri="/", .method=HTTP_GET, .handler=page};
    const httpd_uri_t script_uri = {.uri="/gamepad.js", .method=HTTP_GET, .handler=gamepad_script};
    const httpd_uri_t status_uri = {.uri="/status", .method=HTTP_GET, .handler=status};
    const httpd_uri_t devices_uri = {.uri="/devices", .method=HTTP_GET, .handler=devices};
    const httpd_uri_t select_uri = {.uri="/select", .method=HTTP_POST, .handler=select_device};
    const httpd_uri_t move_uri = {.uri="/move", .method=HTTP_POST, .handler=move};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &page_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &script_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &move_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &devices_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &select_uri));
}
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    { ESP_LOGE(TAG, "NVS requires maintenance; refusing to erase it automatically"); return; }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t wifi = {0};
    if(strlen(CONFIG_RONIN_WIFI_SSID) == 0u)
    { ESP_LOGE(TAG, "Set Wi-Fi SSID in menuconfig"); return; }
    strlcpy((char *)wifi.sta.ssid, CONFIG_RONIN_WIFI_SSID, sizeof(wifi.sta.ssid));
    strlcpy((char *)wifi.sta.password, CONFIG_RONIN_WIFI_PASSWORD, sizeof(wifi.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    start_server();
    esp_err_t mdns_result = mdns_init();
    if(mdns_result == ESP_OK) { mdns_result = mdns_hostname_set("ronin-c6"); }
    if(mdns_result == ESP_OK) { mdns_result = mdns_service_add("Ronin C6", "_http", "_tcp", 80, NULL, 0); }
    if(mdns_result != ESP_OK) { ESP_LOGW(TAG, "mDNS unavailable: %s", esp_err_to_name(mdns_result)); }
    else { ESP_LOGI(TAG, "Open http://ronin-c6.local/"); }
    ronin_ble_start();
}
