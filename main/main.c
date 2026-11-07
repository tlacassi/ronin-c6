#include <stdio.h>
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

static const char *TAG = "ronin";
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_START)) { esp_wifi_connect(); }
    else if((base == WIFI_EVENT) && (id == WIFI_EVENT_STA_DISCONNECTED))
    {
        ronin_ble_stop();
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
static esp_err_t status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, ronin_ble_ready() ? "{\"ble\":true}" : "{\"ble\":false}");
}
static esp_err_t move(httpd_req_t *req)
{
    char body[48] = {0};
    if((req->content_len <= 0) || (req->content_len >= sizeof(body)))
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body"); return ESP_FAIL; }
    int received = 0;
    while(received < req->content_len)
    {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if(n <= 0) { ronin_ble_stop(); return ESP_FAIL; }
        received += n;
    }
    int yaw = 0, pitch = 0, end = 0;
    if((sscanf(body, "%d,%d%n", &yaw, &pitch, &end) != 2) || (body[end] != '\0') ||
       (yaw < -300) || (yaw > 300) || (pitch < -300) || (pitch > 300))
    { ronin_ble_stop(); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid motion"); return ESP_FAIL; }
    if(!ronin_ble_move((int16_t)yaw, (int16_t)pitch))
    { httpd_resp_set_status(req, "503 Service Unavailable"); return httpd_resp_sendstr(req, "BLE unavailable"); }
    return httpd_resp_sendstr(req, "OK");
}
static void start_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.recv_wait_timeout = 1;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    const httpd_uri_t page_uri = {.uri="/", .method=HTTP_GET, .handler=page};
    const httpd_uri_t status_uri = {.uri="/status", .method=HTTP_GET, .handler=status};
    const httpd_uri_t move_uri = {.uri="/move", .method=HTTP_POST, .handler=move};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &page_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &move_uri));
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
    if((strlen(CONFIG_RONIN_WIFI_SSID) == 0u) || (strlen(CONFIG_RONIN_BLE_NAME) == 0u))
    { ESP_LOGE(TAG, "Set Wi-Fi SSID and exact Ronin BLE name in menuconfig"); return; }
    strlcpy((char *)wifi.sta.ssid, CONFIG_RONIN_WIFI_SSID, sizeof(wifi.sta.ssid));
    strlcpy((char *)wifi.sta.password, CONFIG_RONIN_WIFI_PASSWORD, sizeof(wifi.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    start_server();
    ronin_ble_start();
}
