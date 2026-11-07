#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "sdkconfig.h"
#include "duml.h"
#include "ronin_ble.h"

static const char *TAG = "ronin_ble";
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint16_t conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t write_handle = 0u;
static int16_t requested_yaw = 0, requested_pitch = 0;
static int64_t last_command_us = 0;
static uint8_t own_addr_type;
static uint16_t service_start = 0u;
static uint16_t service_end = 0u;
static void scan(void);
static int gap_event(struct ble_gap_event *event, void *arg);

bool ronin_ble_ready(void)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready = (conn != BLE_HS_CONN_HANDLE_NONE) && (write_handle != 0u);
    portEXIT_CRITICAL(&lock);
    return ready;
}
bool ronin_ble_move(int16_t yaw, int16_t pitch)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready = (conn != BLE_HS_CONN_HANDLE_NONE) && (write_handle != 0u);
    if(ready)
    {
        requested_yaw = yaw; requested_pitch = pitch; last_command_us = esp_timer_get_time();
    }
    portEXIT_CRITICAL(&lock);
    return ready;
}
void ronin_ble_stop(void)
{
    portENTER_CRITICAL(&lock);
    requested_yaw = 0; requested_pitch = 0; last_command_us = 0;
    portEXIT_CRITICAL(&lock);
}
static int characteristic_cb(uint16_t ch, const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if((error->status == 0) && (ble_uuid_u16(&chr->uuid.u) == 0xfff5u) &&
       ((chr->properties & BLE_GATT_CHR_PROP_WRITE_NO_RSP) != 0u))
    {
        portENTER_CRITICAL(&lock);
        if(conn == ch) { write_handle = chr->val_handle; }
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "Command characteristic found");
    }
    else if((error->status == BLE_HS_EDONE) && !ronin_ble_ready())
    {
        ESP_LOGW(TAG, "Command characteristic missing");
        ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM);
    }
    else if((error->status != 0) && (error->status != BLE_HS_EDONE))
    {
        ESP_LOGW(TAG, "Characteristic discovery failed: %d", error->status);
        ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM);
    }
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
           (fields.name != NULL) && (strlen(CONFIG_RONIN_BLE_NAME) != 0u) &&
           (fields.name_len == strlen(CONFIG_RONIN_BLE_NAME)) &&
           (memcmp(fields.name, CONFIG_RONIN_BLE_NAME, fields.name_len) == 0))
        {
            if(ble_gap_disc_cancel() == 0)
            {
                int rc = ble_gap_connect(own_addr_type, &event->disc.addr, 30000, NULL, gap_event, NULL);
                if(rc != 0) { ESP_LOGW(TAG, "Connect start failed: %d", rc); scan(); }
            }
        }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status == 0)
        {
            portENTER_CRITICAL(&lock);
            conn = event->connect.conn_handle; write_handle = 0u;
            portEXIT_CRITICAL(&lock);
            service_start = 0u; service_end = 0u;
            ble_uuid16_t uuid = BLE_UUID16_INIT(0xfff0);
            int rc = ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &uuid.u, service_cb, NULL);
            if(rc != 0) { ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM); }
        }
        else { scan(); }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&lock);
        conn = BLE_HS_CONN_HANDLE_NONE; write_handle = 0u;
        requested_yaw = 0; requested_pitch = 0; last_command_us = 0;
        portEXIT_CRITICAL(&lock);
        scan();
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
    if(ble_hs_id_infer_auto(0, &own_addr_type) == 0) { scan(); }
}
static void host_task(void *arg)
{
    (void)arg; nimble_port_run(); nimble_port_freertos_deinit();
}
static void control_task(void *arg)
{
    (void)arg;
    uint16_t seq = 0u;
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
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void ronin_ble_start(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    if(xTaskCreate(control_task, "ronin_control", 4096, NULL, 5, NULL) != pdPASS)
    { ESP_LOGE(TAG, "Control task could not start"); }
}
