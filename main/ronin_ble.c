#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "sdkconfig.h"
#include "nvs.h"
#include "duml.h"
#include "telemetry.h"
#include "ronin_ble.h"

static const char *TAG = "ronin_ble";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t write_handle = 0u;
static int16_t requested_yaw = 0, requested_pitch = 0;
static int64_t last_command_us = 0;
static uint32_t link_session = 0u;
static uint8_t own_addr_type;
static char selected_name[RONIN_NAME_MAX + 1u];
static char device_names[RONIN_SCAN_MAX][RONIN_NAME_MAX + 1u];
static size_t device_count = 0u;
static bool connecting = false;
static bool selecting = false;
static bool host_synced = false;
static uint16_t service_start = 0u;
static uint16_t service_end = 0u;
static uint16_t notify_handle = 0u;
static uint16_t notify_end = 0u;
static uint16_t notify_cccd = 0u;
static telemetry_parser_t telemetry_parser = {0};
static uint8_t battery_percent = 0u;
static int64_t battery_time_us = 0;
static void scan(void);
static int gap_event(struct ble_gap_event *event, void *arg);

size_t ronin_ble_devices(char names[RONIN_SCAN_MAX][RONIN_NAME_MAX + 1u])
{
    size_t count = 0u;
    portENTER_CRITICAL(&lock);
    count = device_count;
    memcpy(names, device_names, sizeof(device_names));
    portEXIT_CRITICAL(&lock);
    return count;
}
void ronin_ble_selected(char name[RONIN_NAME_MAX + 1u])
{
    portENTER_CRITICAL(&lock);
    memcpy(name, selected_name, sizeof(selected_name));
    portEXIT_CRITICAL(&lock);
}
bool ronin_ble_select(const char *name)
{
    bool found = false;
    portENTER_CRITICAL(&lock);
    if((conn == BLE_HS_CONN_HANDLE_NONE) && !connecting)
    {
        for(size_t i = 0u; i < device_count; i++)
        {
            if(strcmp(name, device_names[i]) == 0) { found = true; break; }
        }
    }
    if(found) { selecting = true; }
    portEXIT_CRITICAL(&lock);
    if(!found) { return false; }
    nvs_handle_t store;
    if(nvs_open("ronin", NVS_READWRITE, &store) != ESP_OK)
    { portENTER_CRITICAL(&lock); selecting = false; portEXIT_CRITICAL(&lock); return false; }
    esp_err_t err = nvs_set_str(store, "ble_name", name);
    if(err == ESP_OK) { err = nvs_commit(store); }
    nvs_close(store);
    portENTER_CRITICAL(&lock);
    selecting = false;
    if(err != ESP_OK) { portEXIT_CRITICAL(&lock); return false; }
    strlcpy(selected_name, name, sizeof(selected_name));
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG, "Ronin selected; awaiting advertisement");
    return true;
}
bool ronin_ble_battery(uint8_t *value)
{
    if(value == NULL) { return false; }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool valid = (conn != BLE_HS_CONN_HANDLE_NONE) && (battery_time_us != 0) &&
                 ((now - battery_time_us) <= 10000000);
    if(valid) { *value = battery_percent; }
    portEXIT_CRITICAL(&lock);
    return valid;
}
bool ronin_ble_ready(void)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready = (conn != BLE_HS_CONN_HANDLE_NONE) && (write_handle != 0u);
    portEXIT_CRITICAL(&lock);
    return ready;
}
bool ronin_ble_move(int16_t yaw, int16_t pitch, uint32_t session)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready = (conn != BLE_HS_CONN_HANDLE_NONE) && (write_handle != 0u) && (session == link_session);
    if(ready)
    {
        requested_yaw = yaw; requested_pitch = pitch; last_command_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL(&lock);
    return ready;
}
void ronin_ble_link(bool *ready, uint32_t *session)
{
    portENTER_CRITICAL(&lock);
    *ready = (conn != BLE_HS_CONN_HANDLE_NONE) && (write_handle != 0u);
    *session = link_session;
    portEXIT_CRITICAL(&lock);
}
void ronin_ble_invalidate(void)
{
    portENTER_CRITICAL(&lock);
    link_session++;
    requested_yaw = 0; requested_pitch = 0; last_command_us = 0;
    portEXIT_CRITICAL(&lock);
}
void ronin_ble_stop(void)
{
    portENTER_CRITICAL(&lock);
    requested_yaw = 0; requested_pitch = 0; last_command_us = 0;
    portEXIT_CRITICAL(&lock);
}
static int notify_write_cb(uint16_t ch, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg)
{
    (void)ch; (void)attr; (void)arg;
    if(error->status != 0) { ESP_LOGW(TAG, "Notification subscription failed: %d", error->status); }
    else { ESP_LOGI(TAG, "Telemetry notifications subscribed"); }
    return 0;
}
static int descriptor_cb(uint16_t ch, const struct ble_gatt_error *error,
                         uint16_t chr_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_handle; (void)arg;
    if((error->status == 0) && (ble_uuid_u16(&dsc->uuid.u) == 0x2902u))
    { notify_cccd = dsc->handle; }
    else if((error->status == BLE_HS_EDONE) && (notify_cccd != 0u))
    {
        const uint8_t enable[] = {1u, 0u};
        int rc = ble_gattc_write_flat(ch, notify_cccd, enable, sizeof(enable), notify_write_cb, NULL);
        if(rc != 0) { ESP_LOGW(TAG, "Notification setup failed: %d", rc); }
    }
    else if(error->status != 0)
    { ESP_LOGW(TAG, "Notification descriptor unavailable: %d", error->status); }
    return 0;
}
static int characteristic_cb(uint16_t ch, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if(error->status == 0)
    {
        if((notify_handle != 0u) && (notify_end == 0u) && (chr->def_handle > notify_handle))
        { notify_end = chr->def_handle - 1u; }
        uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
        if((uuid == 0xfff5u) && ((chr->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) != 0u))
        {
            portENTER_CRITICAL(&lock);
            if(conn == ch) { write_handle = chr->val_handle; }
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "Command characteristic found");
        }
        if((uuid == 0xfff4u) && ((chr->properties & BLE_GATT_CHR_PROP_NOTIFY) != 0u))
        { notify_handle = chr->val_handle; }
    }
    else if(error->status == BLE_HS_EDONE)
    {
        if(!ronin_ble_ready())
        { ESP_LOGW(TAG, "Command characteristic missing"); ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM); }
        else if(notify_handle != 0u)
        {
            if(notify_end == 0u) { notify_end = service_end; }
            int rc = ble_gattc_disc_all_dscs(ch, notify_handle, notify_end, descriptor_cb, NULL);
            if(rc != 0) { ESP_LOGW(TAG, "Notification discovery failed: %d", rc); }
        }
        else { ESP_LOGW(TAG, "Telemetry characteristic missing"); }
    }
    else
    { ESP_LOGW(TAG, "Characteristic discovery failed: %d", error->status); ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM); }
    return 0;
}
static int service_cb(uint16_t ch, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if(error->status == 0)
    {
        service_start = svc->start_handle;
        service_end = svc->end_handle;
    }
    else if((error->status == BLE_HS_EDONE) && (service_start != 0u))
    {
        int rc = ble_gattc_disc_all_chrs(ch, service_start, service_end, characteristic_cb, NULL);
        if(rc != 0) { ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM); }
    }
    else
    {
        ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch(event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        struct ble_hs_adv_fields fields = {0};
        if((ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) &&
           (fields.name != NULL) && (fields.name_len > 0u) && (fields.name_len <= RONIN_NAME_MAX))
        {
            char name[RONIN_NAME_MAX + 1u] = {0};
            memcpy(name, fields.name, fields.name_len);
            bool match = false;
            portENTER_CRITICAL(&lock);
            if((strstr(name, "DJI") != NULL) || (strstr(name, "RS 3") != NULL) || (strstr(name, "RS3") != NULL))
            {
                bool known = false;
                for(size_t i = 0u; i < device_count; i++)
                {
                    if(strcmp(name, device_names[i]) == 0) { known = true; break; }
                }
                if(!known && (device_count < RONIN_SCAN_MAX))
                { strlcpy(device_names[device_count++], name, sizeof(device_names[0])); }
            }
            match = !selecting && (selected_name[0] != '\0') && (strcmp(name, selected_name) == 0);
            if(match) { connecting = true; }
            portEXIT_CRITICAL(&lock);
            if(match)
            {
                int rc = ble_gap_disc_cancel();
                if(rc == 0)
                { rc = ble_gap_connect(own_addr_type, &event->disc.addr, 10000, NULL, gap_event, NULL); }
                if(rc != 0)
                {
                    portENTER_CRITICAL(&lock); connecting = false; portEXIT_CRITICAL(&lock);
                    ESP_LOGW(TAG, "Connect start failed: %d", rc);
                    scan();
                }
            }
        }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
        portENTER_CRITICAL(&lock); connecting = false; portEXIT_CRITICAL(&lock);
        if(event->connect.status == 0)
        {
            portENTER_CRITICAL(&lock);
            conn = event->connect.conn_handle; write_handle = 0u; link_session++;
            portEXIT_CRITICAL(&lock);
            service_start = 0u; service_end = 0u;
            notify_handle = 0u; notify_end = 0u; notify_cccd = 0u;
            memset(&telemetry_parser, 0, sizeof(telemetry_parser));
            ble_uuid16_t uuid = BLE_UUID16_INIT(0xfff0);
            int rc = ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &uuid.u, service_cb, NULL);
            if(rc != 0) { ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM); }
        }
        else { scan(); }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&lock);
        connecting = false; link_session++;
        conn = BLE_HS_CONN_HANDLE_NONE; write_handle = 0u;
        requested_yaw = 0; requested_pitch = 0; last_command_us = 0; battery_time_us = 0;
        portEXIT_CRITICAL(&lock);
        scan();
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if((event->notify_rx.conn_handle == conn) && (event->notify_rx.attr_handle == notify_handle))
        {
            uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
            for(uint16_t i = 0u; i < length; i++)
            {
                uint8_t byte = 0u, battery = 0u;
                if(os_mbuf_copydata(event->notify_rx.om, i, 1, &byte) != 0) { break; }
                if(telemetry_feed(&telemetry_parser, byte, &battery))
                {
                    int64_t now = esp_timer_get_time();
                    portENTER_CRITICAL(&lock);
                    battery_percent = battery; battery_time_us = now;
                    portEXIT_CRITICAL(&lock);
                }
            }
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        scan();
        break;
    default: break;
    }
    return 0;
}
static void scan(void)
{
    struct ble_gap_disc_params params = {0};
    params.passive = 0; params.filter_duplicates = 0;
    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if(rc != 0) { ESP_LOGW(TAG, "Scan failed: %d", rc); }
}
static void on_sync(void)
{
    if(ble_hs_id_infer_auto(0, &own_addr_type) == 0)
    {
        portENTER_CRITICAL(&lock); host_synced = true; portEXIT_CRITICAL(&lock);
        scan();
    }
}
static void on_reset(int reason)
{
    portENTER_CRITICAL(&lock);
    host_synced = false; connecting = false; conn = BLE_HS_CONN_HANDLE_NONE; link_session++;
    write_handle = 0u; requested_yaw = 0; requested_pitch = 0; last_command_us = 0; battery_time_us = 0;
    portEXIT_CRITICAL(&lock);
    ESP_LOGW(TAG, "BLE host reset: %d", reason);
}
static void host_task(void *arg)
{
    (void)arg; nimble_port_run(); nimble_port_freertos_deinit();
}
static void control_task(void *arg)
{
    (void)arg;
    uint16_t seq = 0u;
    unsigned retry_ticks = 0u;
    for(;;)
    {
        uint16_t ch, handle;
        int16_t yaw, pitch;
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        ch = conn; handle = write_handle;
        if((last_command_us == 0) || ((now - last_command_us) > 400000))
        { requested_yaw = 0; requested_pitch = 0; }
        yaw = requested_yaw; pitch = requested_pitch;
        portEXIT_CRITICAL(&lock);
        if((ch != BLE_HS_CONN_HANDLE_NONE) && (handle != 0u))
        {
            uint8_t frame[20];
            duml_speed(frame, seq++, yaw, 0, pitch);
            int rc = ble_gattc_write_no_rsp_flat(ch, handle, frame, sizeof(frame));
            if(rc != 0) { ESP_LOGW(TAG, "BLE write failed: %d", rc); ronin_ble_stop(); }
        }
        if(++retry_ticks >= 20u)
        {
            retry_ticks = 0u;
            portENTER_CRITICAL(&lock);
            bool retry_scan = host_synced && !connecting && (conn == BLE_HS_CONN_HANDLE_NONE);
            portEXIT_CRITICAL(&lock);
            if(retry_scan && !ble_gap_disc_active()) { scan(); }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void ronin_ble_start(void)
{
    link_session = esp_random();
    nvs_handle_t store;
    if(nvs_open("ronin", NVS_READONLY, &store) == ESP_OK)
    {
        size_t len = sizeof(selected_name);
        if(nvs_get_str(store, "ble_name", selected_name, &len) != ESP_OK) { selected_name[0] = '\0'; }
        nvs_close(store);
    }
    if(selected_name[0] == '\0') { strlcpy(selected_name, CONFIG_RONIN_BLE_NAME, sizeof(selected_name)); }
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    if(xTaskCreate(control_task, "ronin_control", 4096, NULL, 5, NULL) != pdPASS)
    { ESP_LOGE(TAG, "Control task could not start"); }
}
