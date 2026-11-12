#include <string.h>
#include <stdio.h>
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
typedef struct
{
    ronin_ble_device_t device;
    int64_t last_seen_us;
} scan_entry_t;
static scan_entry_t scan_devices[RONIN_SCAN_MAX];
static size_t device_count = 0u;
static bool connecting = false;
static ble_addr_t target_addr;
static bool target_addr_valid = false;
static ronin_model_t target_model = RONIN_MODEL_UNKNOWN;
static ble_addr_t connecting_addr;
static ble_addr_t connected_addr;
static bool connected_addr_valid = false;
static bool target_forgotten = false;
static bool discovery_mode = false;
static bool host_synced = false;
static uint16_t service_start = 0u;
static uint16_t service_end = 0u;
static uint16_t notify_handle = 0u;
static uint16_t notify_end = 0u;
static uint16_t notify_cccd = 0u;
static bool telemetry_subscribed = false;
static telemetry_parser_t telemetry_parser = {0};
static uint8_t battery_percent = 0u;
static int64_t battery_time_us = 0;
static int64_t sleep_time_us = 0;
static bool sleeping = false;
static uint32_t sleep_session = 0u;
static int16_t latest_pose[3] = {0};
static int64_t pose_time_us = 0;
static int64_t focus_time_us = 0;
static uint16_t focus_position = 0u;
static bool focus_calibrated = false;
static zoom_motion_t zoom_motion = {0};
static uint32_t zoom_session = 0u;
static uint32_t zoom_generation = 0u;
static bool zoom_task_started = false;
static void zoom_task(void *arg);
static telemetry_pose_format_t latest_pose_format = TELEMETRY_POSE_NONE;
static uint32_t rx_notifications = 0u, rx_frames = 0u, rx_poses = 0u;
static int32_t last_disconnect_reason = 0;
static void scan(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static int service_cb(uint16_t ch, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg);
static bool address_equal(const ble_addr_t *a, const ble_addr_t *b);
static void format_address_id(const ble_addr_t *address, char id[RONIN_DEVICE_ID_MAX]);
static ronin_model_t model_from_name(const char *name);
static bool persist_target(const ronin_ble_device_t *device);

#if CONFIG_RONIN_BLE_TRACE
#define RONIN_TRACE_DEPTH 8u
typedef struct
{
    uint16_t length;
    uint8_t data[TELEMETRY_FRAME_MAX];
} trace_frame_t;
static trace_frame_t trace_frames[RONIN_TRACE_DEPTH];
static unsigned trace_head = 0u, trace_count = 0u;

/* Defer serial output to the control task so the BLE host never waits for it. */
static void trace_enqueue(const telemetry_parser_t *parser)
{
    size_t length = parser->frame[1] | ((parser->frame[2] & 3u) << 8u);
    portENTER_CRITICAL(&lock);
    if(trace_count < RONIN_TRACE_DEPTH)
    {
        unsigned index = (trace_head + trace_count) % RONIN_TRACE_DEPTH;
        trace_frames[index].length = (uint16_t)length;
        memcpy(trace_frames[index].data, parser->frame, length);
        trace_count++;
    }
    portEXIT_CRITICAL(&lock);
}
static void trace_drain(void)
{
    for(unsigned i = 0u; i < 4u; i++)
    {
        trace_frame_t frame = {0};
        portENTER_CRITICAL(&lock);
        bool available = trace_count > 0u;
        if(available)
        {
            frame = trace_frames[trace_head];
            trace_head = (trace_head + 1u) % RONIN_TRACE_DEPTH;
            trace_count--;
        }
        portEXIT_CRITICAL(&lock);
        if(!available) { break; }
        ESP_LOG_BUFFER_HEX_LEVEL("ronin_rx", frame.data, frame.length, ESP_LOG_INFO);
    }
}
#endif

static bool control_allowed_locked(void)
{
    return (target_model == RONIN_MODEL_RS3) || (target_model == RONIN_MODEL_RS4_PRO);
}
static bool pose_format_matches_model(ronin_model_t model, telemetry_pose_format_t format)
{
    return ((model == RONIN_MODEL_RS3) && (format == TELEMETRY_POSE_TLV)) ||
           ((model == RONIN_MODEL_RS4_PRO) && (format == TELEMETRY_POSE_PARAMS49));
}
static bool address_equal(const ble_addr_t *a, const ble_addr_t *b)
{
    return (a->type == b->type) && (memcmp(a->val, b->val, sizeof(a->val)) == 0);
}
static bool link_matches_target_locked(void)
{
    return !discovery_mode && (conn != BLE_HS_CONN_HANDLE_NONE) && connected_addr_valid && target_addr_valid &&
           address_equal(&connected_addr, &target_addr);
}
static void format_address_id(const ble_addr_t *address, char id[RONIN_DEVICE_ID_MAX])
{
    snprintf(id, RONIN_DEVICE_ID_MAX, "%02X:%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)address->type, (unsigned)address->val[0], (unsigned)address->val[1],
             (unsigned)address->val[2], (unsigned)address->val[3], (unsigned)address->val[4],
             (unsigned)address->val[5]);
}
void ronin_ble_device_id(const ronin_ble_device_t *device, char id[RONIN_DEVICE_ID_MAX])
{
    if((device == NULL) || (id == NULL)) { return; }
    ble_addr_t address = {.type = device->address_type};
    memcpy(address.val, device->address, sizeof(address.val));
    format_address_id(&address, id);
}
static ronin_model_t model_from_name(const char *name)
{
    if((strstr(name, "RS 3") != NULL) || (strstr(name, "RS3") != NULL)) { return RONIN_MODEL_RS3; }
    if((strstr(name, "RS 4") != NULL) || (strstr(name, "RS4") != NULL))
    {
        if((strstr(name, "PRO") != NULL) || (strstr(name, "Pro") != NULL)) { return RONIN_MODEL_RS4_PRO; }
        return RONIN_MODEL_RS4;
    }
    if((strstr(name, "RS 5") != NULL) || (strstr(name, "RS5") != NULL)) { return RONIN_MODEL_RS5; }
    return RONIN_MODEL_UNKNOWN;
}
static bool address_matches(const uint8_t address[6], uint8_t type, const ble_addr_t *other)
{
    return (type == other->type) && (memcmp(address, other->val, 6u) == 0);
}
size_t ronin_ble_devices(ronin_ble_device_t devices[RONIN_SCAN_MAX])
{
    int64_t now = esp_timer_get_time();
    size_t count = 0u;
    portENTER_CRITICAL(&lock);
    for(size_t i = 0u; i < device_count; i++)
    {
        bool is_target = target_addr_valid && address_matches(scan_devices[i].device.address,
                                                               scan_devices[i].device.address_type,
                                                               &target_addr);
        bool is_connected = (conn != BLE_HS_CONN_HANDLE_NONE) && connected_addr_valid &&
                            address_matches(scan_devices[i].device.address,
                                            scan_devices[i].device.address_type,
                                            &connected_addr);
        bool recent = (now - scan_devices[i].last_seen_us) <= 10000000;
        if(recent || is_target || is_connected)
        {
            devices[count] = scan_devices[i].device;
            devices[count].visible = recent;
            devices[count].is_target = is_target;
            devices[count].is_connected = is_connected;
            count++;
        }
    }
    portEXIT_CRITICAL(&lock);
    return count;
}
static bool persist_target(const ronin_ble_device_t *device)
{
    nvs_handle_t store;
    if(nvs_open("ronin", NVS_READWRITE, &store) != ESP_OK) { return false; }
    esp_err_t err = nvs_set_str(store, "ble_name", device->name);
    if(err == ESP_OK) { err = nvs_set_blob(store, "ble_addr", device->address, sizeof(device->address)); }
    if(err == ESP_OK) { err = nvs_set_u8(store, "ble_addr_type", device->address_type); }
    if(err == ESP_OK) { err = nvs_set_u8(store, "ble_forgot", 0u); }
    if(err == ESP_OK) { err = nvs_commit(store); }
    nvs_close(store);
    return err == ESP_OK;
}
bool ronin_ble_select(const char *device_id)
{
    if(device_id == NULL) { return false; }
    ronin_ble_device_t devices[RONIN_SCAN_MAX] = {0};
    size_t count = ronin_ble_devices(devices);
    ronin_ble_device_t chosen = {0};
    bool found = false;
    for(size_t i = 0u; i < count; i++)
    {
        char id[RONIN_DEVICE_ID_MAX] = {0};
        ronin_ble_device_id(&devices[i], id);
        if((strcmp(id, device_id) == 0) && devices[i].visible)
        {
            chosen = devices[i];
            found = true;
            break;
        }
    }
    if(!found || !persist_target(&chosen)) { return false; }

    ble_addr_t chosen_addr = {.type = chosen.address_type};
    memcpy(chosen_addr.val, chosen.address, sizeof(chosen_addr.val));
    uint16_t connected_handle = BLE_HS_CONN_HANDLE_NONE;
    bool same_target = false;
    bool connection_in_progress = false;
    bool bluetooth_ready = false;
    portENTER_CRITICAL(&lock);
    same_target = target_addr_valid && address_equal(&target_addr, &chosen_addr);
    target_addr = chosen_addr;
    target_addr_valid = true;
    target_model = chosen.model;
    target_forgotten = false;
    discovery_mode = false;
    strlcpy(selected_name, chosen.name, sizeof(selected_name));
    if(!same_target)
    {
        link_session++;
        requested_yaw = 0;
        requested_pitch = 0;
        last_command_us = 0;
        battery_time_us = 0;
        pose_time_us = 0; focus_time_us = 0;
        write_handle = 0u;
    }
    if((conn != BLE_HS_CONN_HANDLE_NONE) && connected_addr_valid && !address_equal(&connected_addr, &chosen_addr))
    { connected_handle = conn; }
    connection_in_progress = connecting;
    bluetooth_ready = host_synced;
    portEXIT_CRITICAL(&lock);

    if(connected_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        int rc = ble_gap_terminate(connected_handle, BLE_ERR_REM_USER_CONN_TERM);
        if(rc != 0) { ESP_LOGW(TAG, "Old target disconnect request failed: %d", rc); }
    }
    else if(!connection_in_progress && bluetooth_ready && !ble_gap_disc_active())
    { scan(); }
    ESP_LOGI(TAG, "BLE target selected: %s", chosen.name);
    return true;
}
void ronin_ble_begin_discovery(void)
{
    uint16_t connected_handle = BLE_HS_CONN_HANDLE_NONE;
    bool bluetooth_ready = false;
    portENTER_CRITICAL(&lock);
    discovery_mode = true;
    link_session++;
    requested_yaw = 0;
    requested_pitch = 0;
    last_command_us = 0;
    write_handle = 0u;
    if(conn != BLE_HS_CONN_HANDLE_NONE) { connected_handle = conn; }
    bluetooth_ready = host_synced;
    portEXIT_CRITICAL(&lock);

    if(connected_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        int rc = ble_gap_terminate(connected_handle, BLE_ERR_REM_USER_CONN_TERM);
        if(rc != 0) { ESP_LOGW(TAG, "Target pause disconnect request failed: %d", rc); }
    }
    else if(bluetooth_ready && !ble_gap_disc_active())
    { scan(); }
}
void ronin_ble_resume_target(void)
{
    bool bluetooth_ready = false;
    bool connection_in_progress = false;
    bool disconnected = false;
    portENTER_CRITICAL(&lock);
    discovery_mode = false;
    bluetooth_ready = host_synced;
    connection_in_progress = connecting;
    disconnected = (conn == BLE_HS_CONN_HANDLE_NONE);
    portEXIT_CRITICAL(&lock);

    if(bluetooth_ready && disconnected && !connection_in_progress && !ble_gap_disc_active())
    { scan(); }
}
bool ronin_ble_forget(void)
{
    nvs_handle_t store;
    if(nvs_open("ronin", NVS_READWRITE, &store) != ESP_OK) { return false; }
    esp_err_t err = nvs_erase_key(store, "ble_name");
    if((err == ESP_OK) || (err == ESP_ERR_NVS_NOT_FOUND)) { err = nvs_erase_key(store, "ble_addr"); }
    if((err == ESP_OK) || (err == ESP_ERR_NVS_NOT_FOUND)) { err = nvs_erase_key(store, "ble_addr_type"); }
    if((err == ESP_OK) || (err == ESP_ERR_NVS_NOT_FOUND)) { err = nvs_set_u8(store, "ble_forgot", 1u); }
    if(err == ESP_OK) { err = nvs_commit(store); }
    nvs_close(store);
    if(err != ESP_OK) { return false; }

    uint16_t connected_handle = BLE_HS_CONN_HANDLE_NONE;
    bool connection_in_progress = false;
    bool bluetooth_ready = false;
    portENTER_CRITICAL(&lock);
    selected_name[0] = '\0';
    target_addr_valid = false;
    target_model = RONIN_MODEL_UNKNOWN;
    target_forgotten = true;
    discovery_mode = false;
    link_session++;
    requested_yaw = 0;
    requested_pitch = 0;
    last_command_us = 0;
    battery_time_us = 0;
    pose_time_us = 0; focus_time_us = 0;
    write_handle = 0u;
    if(conn != BLE_HS_CONN_HANDLE_NONE) { connected_handle = conn; }
    connection_in_progress = connecting;
    bluetooth_ready = host_synced;
    portEXIT_CRITICAL(&lock);

    if(connected_handle != BLE_HS_CONN_HANDLE_NONE)
    {
        int rc = ble_gap_terminate(connected_handle, BLE_ERR_REM_USER_CONN_TERM);
        if(rc != 0) { ESP_LOGW(TAG, "Disconnect request failed: %d", rc); }
    }
    else if(!connection_in_progress && bluetooth_ready && !ble_gap_disc_active())
    { scan(); }
    ESP_LOGI(TAG, "Saved BLE target forgotten");
    return true;
}
void ronin_ble_selected(char name[RONIN_NAME_MAX + 1u])
{
    portENTER_CRITICAL(&lock);
    memcpy(name, selected_name, sizeof(selected_name));
    portEXIT_CRITICAL(&lock);
}
void ronin_ble_target(char id[RONIN_DEVICE_ID_MAX], char name[RONIN_NAME_MAX + 1u], bool *connected, bool *ready)
{
    if((id == NULL) || (name == NULL) || (connected == NULL) || (ready == NULL)) { return; }
    ble_addr_t target = {0};
    bool target_valid;
    bool link_connected;
    bool link_ready;
    portENTER_CRITICAL(&lock);
    target = target_addr;
    target_valid = target_addr_valid;
    memcpy(name, selected_name, sizeof(selected_name));
    link_connected = !discovery_mode && (conn != BLE_HS_CONN_HANDLE_NONE) && connected_addr_valid &&
                     target_addr_valid && address_equal(&connected_addr, &target_addr);
    link_ready = link_connected && (write_handle != 0u);
    portEXIT_CRITICAL(&lock);
    id[0] = '\0';
    if(target_valid) { format_address_id(&target, id); }
    *connected = link_connected;
    *ready = link_ready;
}
bool ronin_ble_protocol_verified(void)
{
    portENTER_CRITICAL(&lock);
    bool verified = target_model == RONIN_MODEL_RS3;
    portEXIT_CRITICAL(&lock);
    return verified;
}
bool ronin_ble_control_enabled(void)
{
    portENTER_CRITICAL(&lock);
    bool enabled = control_allowed_locked();
    portEXIT_CRITICAL(&lock);
    return enabled;
}
ronin_model_t ronin_ble_selected_model(void)
{
    portENTER_CRITICAL(&lock);
    ronin_model_t model = target_model;
    portEXIT_CRITICAL(&lock);
    return model;
}
bool ronin_ble_pose(int16_t pose[3])
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool known_pose = pose_format_matches_model(target_model, latest_pose_format);
    bool valid = known_pose && link_matches_target_locked() && (pose_time_us != 0) && ((now - pose_time_us) < 3000000);
    if(valid) { memcpy(pose, latest_pose, sizeof(latest_pose)); }
    portEXIT_CRITICAL(&lock);
    return valid;
}
bool ronin_ble_battery(uint8_t *value)
{
    if(value == NULL) { return false; }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool known_battery = (target_model == RONIN_MODEL_RS3) || (target_model == RONIN_MODEL_RS4_PRO);
    bool valid = known_battery && link_matches_target_locked() && (battery_time_us != 0) &&
                 ((now - battery_time_us) <= 10000000);
    if(valid) { *value = battery_percent; }
    portEXIT_CRITICAL(&lock);
    return valid;
}
bool ronin_ble_ready(void)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready = link_matches_target_locked() && (write_handle != 0u);
    portEXIT_CRITICAL(&lock);
    return ready;
}
bool ronin_ble_move(int16_t yaw, int16_t pitch, uint32_t session)
{
    bool ready;
    portENTER_CRITICAL(&lock);
    ready = control_allowed_locked() && link_matches_target_locked() &&
            (write_handle != 0u) && (session == link_session);
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
    *ready = link_matches_target_locked() && (write_handle != 0u);
    *session = link_session;
    portEXIT_CRITICAL(&lock);
}
void ronin_ble_invalidate(void)
{
    portENTER_CRITICAL(&lock);
    link_session++;
    requested_yaw = 0; requested_pitch = 0; last_command_us = 0;
    if(zoom_motion.active) { zoom_motion_cancel(&zoom_motion, ZOOM_ERROR_UNAVAILABLE); }
    portEXIT_CRITICAL(&lock);
}
bool ronin_ble_sleep_state(bool *value)
{
    if(value == NULL) { return false; }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool valid = (target_model == RONIN_MODEL_RS3) && link_matches_target_locked() &&
                 (sleep_session == link_session) && (sleep_time_us != 0) &&
                 ((now - sleep_time_us) <= 10000000);
    if(valid) { *value = sleeping; }
    portEXIT_CRITICAL(&lock);
    return valid;
}
bool ronin_ble_set_sleep(uint32_t session, bool sleep)
{
    uint16_t ch = 0u, handle = 0u;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool ok = (target_model == RONIN_MODEL_RS3) && link_matches_target_locked() &&
              (write_handle != 0u) && (session == link_session) &&
              (sleep_session == session) && (sleep_time_us != 0) &&
              ((now - sleep_time_us) <= 10000000) && (sleeping != sleep) &&
              (requested_yaw == 0) && (requested_pitch == 0) && !zoom_motion.active;
    if(ok)
    {
        ch = conn; handle = write_handle;
        sleep_time_us = 0;
    }
    portEXIT_CRITICAL(&lock);
    if(!ok) { return false; }
    uint8_t frame[16] = {0};
    duml_sleep(frame, 0u, sleep);
    portENTER_CRITICAL(&lock);
    bool current = (target_model == RONIN_MODEL_RS3) && link_matches_target_locked() &&
                   (conn == ch) && (write_handle == handle) && (link_session == session);
    portEXIT_CRITICAL(&lock);
    return current && (ble_gattc_write_no_rsp_flat(ch, handle, frame, sizeof(frame)) == 0);
}
bool ronin_ble_goto(uint32_t session, const int16_t pose[3], uint8_t duration)
{
    if((pose == NULL) || (duration < 5u) || (duration > 50u)) { return false; }
    for(unsigned i = 0u; i < 3u; i++)
    {
        if((pose[i] < -1800) || (pose[i] > 1800)) { return false; }
    }
    int16_t pitch = pose[0];
    int64_t now = esp_timer_get_time();
    uint16_t ch = 0u, h = 0u;
    portENTER_CRITICAL(&lock);
    bool ok = control_allowed_locked() && pose_format_matches_model(target_model, latest_pose_format) &&
              link_matches_target_locked() &&
              (write_handle != 0u) && (session == link_session) &&
              (pose_time_us != 0) && ((now - pose_time_us) < 3000000) && (requested_yaw == 0) && (requested_pitch == 0);
    if(ok)
    {
        ch = conn; h = write_handle;
        /* Pro attitude tilt and the absolute command use opposite signs.
           An isolated +3 deg wire target measured -3 deg with pan/roll fixed. */
        if(target_model == RONIN_MODEL_RS4_PRO) { pitch = (int16_t)-pose[0]; }
    }
    portEXIT_CRITICAL(&lock);
    if(!ok) { return false; }
    uint8_t frame[21] = {0};
    duml_angle(frame, 0u, pose[2], pose[1], pitch, 0x01u, duration);
    portENTER_CRITICAL(&lock);
    bool still_current = control_allowed_locked() && link_matches_target_locked() &&
                         (conn == ch) && (write_handle == h) && (link_session == session);
    portEXIT_CRITICAL(&lock);
    if(!still_current) { return false; }
    return ble_gattc_write_no_rsp_flat(ch, h, frame, sizeof(frame)) == 0;
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
    (void)attr; (void)arg;
    if(error->status != 0) { ESP_LOGW(TAG, "Notification subscription failed: %d", error->status); }
    else
    {
        portENTER_CRITICAL(&lock);
        if(conn == ch) { telemetry_subscribed = true; }
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "Telemetry notifications subscribed");
    }
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
static int mtu_cb(uint16_t ch, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "MTU=%u status=%d", mtu, err->status);
    ble_uuid16_t uuid = BLE_UUID16_INIT(0xfff0);
    int rc = ble_gattc_disc_svc_by_uuid(ch, &uuid.u, service_cb, NULL);
    if(rc != 0) { ble_gap_terminate(ch, BLE_ERR_REM_USER_CONN_TERM); }
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
static bool record_device(const ble_addr_t *address, const char *name, int8_t rssi)
{
    ronin_model_t model = model_from_name(name);
    int64_t now = esp_timer_get_time();
    size_t slot = RONIN_SCAN_MAX;
    portENTER_CRITICAL(&lock);
    for(size_t i = 0u; i < device_count; i++)
    {
        if((scan_devices[i].device.address_type == address->type) &&
           (memcmp(scan_devices[i].device.address, address->val, sizeof(address->val)) == 0))
        { slot = i; break; }
    }
    if((slot == RONIN_SCAN_MAX) && (model == RONIN_MODEL_UNKNOWN))
    {
        portEXIT_CRITICAL(&lock);
        return false;
    }
    if(slot == RONIN_SCAN_MAX)
    {
        if(device_count < RONIN_SCAN_MAX)
        { slot = device_count++; }
        else
        {
            int64_t oldest = now;
            for(size_t i = 0u; i < device_count; i++)
            {
                bool is_target = target_addr_valid && address_matches(scan_devices[i].device.address,
                                                                       scan_devices[i].device.address_type,
                                                                       &target_addr);
                bool is_connected = connected_addr_valid && address_matches(scan_devices[i].device.address,
                                                                             scan_devices[i].device.address_type,
                                                                             &connected_addr);
                if(!is_target && !is_connected && (scan_devices[i].last_seen_us < oldest))
                { oldest = scan_devices[i].last_seen_us; slot = i; }
            }
        }
    }
    if((slot == RONIN_SCAN_MAX) || ((model == RONIN_MODEL_UNKNOWN) &&
                                  (scan_devices[slot].device.name[0] == '\0')))
    {
        portEXIT_CRITICAL(&lock);
        return false;
    }
    ronin_ble_device_t *device = &scan_devices[slot].device;
    memcpy(device->address, address->val, sizeof(device->address));
    device->address_type = address->type;
    device->rssi = rssi;
    if(model != RONIN_MODEL_UNKNOWN)
    {
        device->model = model;
        strlcpy(device->name, name, sizeof(device->name));
    }
    scan_devices[slot].last_seen_us = now;
    portEXIT_CRITICAL(&lock);
    return true;
}
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch(event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        struct ble_hs_adv_fields fields = {0};
        char name[RONIN_NAME_MAX + 1u] = {0};
        if((ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) &&
           (fields.name != NULL) && (fields.name_len > 0u) && (fields.name_len <= RONIN_NAME_MAX))
        {
            memcpy(name, fields.name, fields.name_len);
        }
        bool known_device = record_device(&event->disc.addr, name, event->disc.rssi);
        bool match = false;
        portENTER_CRITICAL(&lock);
        if(!discovery_mode && target_addr_valid)
        { match = address_equal(&target_addr, &event->disc.addr); }
        else if(!discovery_mode && !target_forgotten && !target_addr_valid &&
                (selected_name[0] != '\0') && (name[0] != '\0') &&
                (strcmp(name, selected_name) == 0) && (model_from_name(name) != RONIN_MODEL_UNKNOWN))
        {
            target_addr = event->disc.addr;
            target_addr_valid = true;
            target_model = model_from_name(name);
            match = true;
        }
        if(match && !connecting && (conn == BLE_HS_CONN_HANDLE_NONE))
        {
            connecting = true;
            connecting_addr = event->disc.addr;
        }
        else { match = false; }
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
        else if(!known_device && (name[0] != '\0'))
        { ESP_LOGD(TAG, "Ignoring unsupported BLE device name"); }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
    {
        bool accepted = false;
        portENTER_CRITICAL(&lock);
        connecting = false;
        if(event->connect.status == 0)
        {
            telemetry_subscribed = false;
            rx_notifications = 0u; rx_frames = 0u; rx_poses = 0u;
            conn = event->connect.conn_handle;
            connected_addr = connecting_addr;
            connected_addr_valid = true;
            write_handle = 0u;
            link_session++;
            requested_yaw = 0;
            requested_pitch = 0;
            last_command_us = 0;
            battery_time_us = 0;
            pose_time_us = 0; focus_time_us = 0;
            accepted = !discovery_mode && target_addr_valid && address_equal(&connecting_addr, &target_addr);
        }
        portEXIT_CRITICAL(&lock);
        if(event->connect.status == 0)
        {
            if(!accepted)
            {
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                break;
            }
            service_start = 0u; service_end = 0u;
            notify_handle = 0u; notify_end = 0u; notify_cccd = 0u;
            memset(&telemetry_parser, 0, sizeof(telemetry_parser));
            int rc = ble_gattc_exchange_mtu(event->connect.conn_handle, mtu_cb, NULL);
            if(rc != 0) { ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM); }
        }
        else
        {
            ESP_LOGW(TAG, "BLE connection failed: %d", event->connect.status);
            scan();
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "BLE disconnected: reason=%d", event->disconnect.reason);
        portENTER_CRITICAL(&lock);
        last_disconnect_reason = event->disconnect.reason;
        connecting = false; link_session++;
        conn = BLE_HS_CONN_HANDLE_NONE; write_handle = 0u;
        connected_addr_valid = false;
        telemetry_subscribed = false;
        requested_yaw = 0; requested_pitch = 0; last_command_us = 0; battery_time_us = 0; pose_time_us = 0; focus_time_us = 0;
        portEXIT_CRITICAL(&lock);
        scan();
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if((event->notify_rx.conn_handle == conn) && (event->notify_rx.attr_handle == notify_handle))
        {
            portENTER_CRITICAL(&lock);
            rx_notifications++;
            portEXIT_CRITICAL(&lock);
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
                if(telemetry_parser.frame_valid)
                {
                    portENTER_CRITICAL(&lock);
                    rx_frames++;
                    portEXIT_CRITICAL(&lock);
#if CONFIG_RONIN_BLE_TRACE
                    trace_enqueue(&telemetry_parser);
#endif
                }
                if(telemetry_parser.focus_updated)
                {
                    int64_t now = esp_timer_get_time();
                    portENTER_CRITICAL(&lock);
                    if((telemetry_parser.focus_address == ronin_model_zoom_address(target_model)) &&
                       ronin_model_supports_zoom(target_model) && link_matches_target_locked())
                    {
                        focus_position = telemetry_parser.focus_position;
                        focus_calibrated = telemetry_parser.focus_calibrated;
                        focus_time_us = now;
                    }
                    portEXIT_CRITICAL(&lock);
                }
                if(telemetry_parser.sleep_updated)
                {
                    int64_t now = esp_timer_get_time();
                    portENTER_CRITICAL(&lock);
                    if((target_model == RONIN_MODEL_RS3) && link_matches_target_locked())
                    {
                        sleeping = telemetry_parser.sleeping;
                        sleep_time_us = now;
                        sleep_session = link_session;
                    }
                    portEXIT_CRITICAL(&lock);
                }
                if(telemetry_parser.pose_updated)
                {
                    int64_t now = esp_timer_get_time();
                    portENTER_CRITICAL(&lock);
                    bool known_pose = pose_format_matches_model(target_model, telemetry_parser.pose_format);
                    if(known_pose && link_matches_target_locked())
                    {
                        memcpy(latest_pose, telemetry_parser.pose, sizeof(latest_pose)); pose_time_us = now;
                        latest_pose_format = telemetry_parser.pose_format;
                        rx_poses++;
                    }
                    portEXIT_CRITICAL(&lock);
                }
            }
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
    {
        portENTER_CRITICAL(&lock);
        bool retry_scan = host_synced && !connecting && (conn == BLE_HS_CONN_HANDLE_NONE);
        portEXIT_CRITICAL(&lock);
        if(retry_scan) { scan(); }
        break;
    }
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
    connected_addr_valid = false;
    telemetry_subscribed = false;
    write_handle = 0u; requested_yaw = 0; requested_pitch = 0; last_command_us = 0; battery_time_us = 0; pose_time_us = 0; focus_time_us = 0;
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
    unsigned release_frames = 0u;
    unsigned poll_stage = 0u;
    uint32_t poll_session = 0u;
    uint32_t motion_session = 0u;
    int64_t last_poll_us = 0;
    bool was_moving = false;
    for(;;)
    {
        uint16_t ch, handle;
        uint32_t send_session;
        int16_t yaw, pitch;
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&lock);
        ch = conn; handle = write_handle;
        send_session = link_session;
        if((last_command_us == 0) || ((now - last_command_us) > 400000))
        { requested_yaw = 0; requested_pitch = 0; }
        yaw = requested_yaw; pitch = requested_pitch;
        portEXIT_CRITICAL(&lock);
        if(motion_session != send_session)
        {
            motion_session = send_session;
            was_moving = false;
            release_frames = 0u;
        }
        bool moving = (yaw != 0) || (pitch != 0);
        if(moving) { was_moving = true; release_frames = 0u; }
        else if(was_moving) { was_moving = false; release_frames = 3u; }
        if((ch != BLE_HS_CONN_HANDLE_NONE) && (handle != 0u) && (moving || (release_frames > 0u)))
        {
            portENTER_CRITICAL(&lock);
            bool still_current = control_allowed_locked() && link_matches_target_locked() && (conn == ch) &&
                                 (write_handle == handle) && (link_session == send_session);
            portEXIT_CRITICAL(&lock);
            if(!still_current)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            uint8_t frame[20];
            duml_speed(frame, seq++, yaw, 0, pitch);
            if(!moving)
            {
                frame[17] = 0u;
                uint16_t crc = duml_crc16(frame, 18u);
                frame[18] = (uint8_t)crc; frame[19] = (uint8_t)(crc >> 8u);
                release_frames--;
            }
            int rc = ble_gattc_write_no_rsp_flat(ch, handle, frame, sizeof(frame));
            if(rc != 0) { ESP_LOGW(TAG, "BLE write failed: %d", rc); ronin_ble_stop(); }
        }
        if(poll_session != send_session)
        {
            poll_session = send_session;
            poll_stage = 0u;
            last_poll_us = now;
        }
        portENTER_CRITICAL(&lock);
        bool poll_ready = telemetry_subscribed && (target_model == RONIN_MODEL_RS3) &&
                          link_matches_target_locked() && (link_session == send_session) &&
                          (conn == ch) && (write_handle == handle) && (handle != 0u);
        portEXIT_CRITICAL(&lock);
        if(poll_ready && ((now - last_poll_us) >= 1000000))
        {
            uint8_t frame[DUML_STATUS_POLL_MAX] = {0};
            size_t length = duml_status_poll(frame, seq++, poll_stage);
            int rc = ble_gattc_write_no_rsp_flat(ch, handle, frame, length);
            if(rc == 0)
            {
                if(poll_stage < 2u) { poll_stage++; }
            }
            else { ESP_LOGW(TAG, "Status poll failed: %d", rc); }
            last_poll_us = now;
        }
        if(++retry_ticks >= 20u)
        {
            retry_ticks = 0u;
            portENTER_CRITICAL(&lock);
            bool retry_scan = host_synced && !connecting && (conn == BLE_HS_CONN_HANDLE_NONE);
            portEXIT_CRITICAL(&lock);
            if(retry_scan && !ble_gap_disc_active()) { scan(); }
        }
#if CONFIG_RONIN_BLE_TRACE
        trace_drain();
#endif
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void ronin_ble_start(void)
{
    link_session = esp_random();
    nvs_handle_t store;
    if(nvs_open("ronin", NVS_READONLY, &store) == ESP_OK)
    {
        uint8_t forgotten = 0u;
        if((nvs_get_u8(store, "ble_forgot", &forgotten) == ESP_OK) && (forgotten != 0u))
        { target_forgotten = true; }
        if(!target_forgotten)
        {
            size_t len = sizeof(selected_name);
            if(nvs_get_str(store, "ble_name", selected_name, &len) != ESP_OK) { selected_name[0] = '\0'; }
            size_t address_length = sizeof(target_addr.val);
            uint8_t address_type = 0u;
            if((nvs_get_blob(store, "ble_addr", target_addr.val, &address_length) == ESP_OK) &&
               (address_length == sizeof(target_addr.val)) &&
               (nvs_get_u8(store, "ble_addr_type", &address_type) == ESP_OK))
            {
                target_addr.type = address_type;
                target_addr_valid = true;
            }
        }
        nvs_close(store);
    }
    if(!target_forgotten && (selected_name[0] == '\0'))
    { strlcpy(selected_name, CONFIG_RONIN_BLE_NAME, sizeof(selected_name)); }
    target_model = model_from_name(selected_name);
    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    BaseType_t zoom_created = xTaskCreate(zoom_task, "ronin_zoom", 3072, NULL, 5, NULL);
    portENTER_CRITICAL(&lock);
    zoom_task_started = zoom_created == pdPASS;
    portEXIT_CRITICAL(&lock);
    if(zoom_created != pdPASS) { ESP_LOGE(TAG, "Zoom task could not start"); }
    if(xTaskCreate(control_task, "ronin_control", 4096, NULL, 5, NULL) != pdPASS)
    { ESP_LOGE(TAG, "Control task could not start"); }
}

void ronin_ble_diagnostics(ronin_ble_diagnostics_t *diagnostics)
{
    if(diagnostics == NULL) { return; }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    diagnostics->notifications = rx_notifications;
    diagnostics->frames = rx_frames;
    diagnostics->poses = rx_poses;
    diagnostics->disconnect_reason = last_disconnect_reason;
    diagnostics->subscribed = telemetry_subscribed && link_matches_target_locked();
    int64_t age_ms = (now - pose_time_us) / 1000;
    diagnostics->pose_age_ms = pose_time_us == 0 ? -1 : (age_ms > INT32_MAX ? INT32_MAX : (int32_t)age_ms);
    portEXIT_CRITICAL(&lock);
}

static bool zoom_available_locked(int64_t now)
{
    return zoom_task_started && ronin_model_supports_zoom(target_model) && link_matches_target_locked() &&
           (write_handle != 0u) && (focus_time_us != 0) && ((now - focus_time_us) < 1000000) && focus_calibrated;
}
void ronin_ble_zoom_status(ronin_zoom_status_t *status)
{
    if(status == NULL) { return; }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    status->present = ronin_model_supports_zoom(target_model) && link_matches_target_locked() &&
                      (focus_time_us != 0) && ((now - focus_time_us) < 3000000);
    status->calibrated = status->present && focus_calibrated;
    status->position = status->calibrated ? focus_position : 0u;
    status->available = zoom_available_locked(now);
    status->moving = zoom_motion.active && (zoom_session == link_session) && status->available;
    status->target = zoom_motion.target;
    status->error = zoom_motion.error;
    portEXIT_CRITICAL(&lock);
}
bool ronin_ble_zoom_goto(uint32_t session, uint16_t target, uint8_t speed)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool valid = zoom_available_locked(now) && (session == link_session);
    if(valid)
    {
        valid = zoom_motion_start(&zoom_motion, focus_position, target, speed, (uint32_t)(now / 1000));
        if(valid) { zoom_session = session; zoom_generation++; }
    }
    portEXIT_CRITICAL(&lock);
    return valid;
}
bool ronin_ble_zoom_keepalive(uint32_t session)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    bool valid = zoom_available_locked(now) && (session == link_session) && (session == zoom_session);
    if(valid) { zoom_motion_renew(&zoom_motion, (uint32_t)(now / 1000)); }
    portEXIT_CRITICAL(&lock);
    return valid;
}
bool ronin_ble_zoom_stop(uint32_t session)
{
    portENTER_CRITICAL(&lock);
    bool valid = session == link_session;
    if(valid) { zoom_motion_cancel(&zoom_motion, ZOOM_ERROR_NONE); zoom_generation++; }
    portEXIT_CRITICAL(&lock);
    return valid;
}
static void zoom_task(void *arg)
{
    (void)arg;
    uint16_t sequence = 0u;
    TickType_t wake = xTaskGetTickCount();
    for(;;)
    {
        int64_t now = esp_timer_get_time();
        uint16_t ch = 0u, handle = 0u, position = 0u;
        uint8_t motor_address = 0u;
        uint32_t session = 0u, generation = 0u;
        portENTER_CRITICAL(&lock);
        bool valid = zoom_available_locked(now) && (zoom_session == link_session);
        bool send = zoom_motion_tick(&zoom_motion, (uint32_t)(now / 1000), valid, &position);
        if(send)
        {
            ch = conn; handle = write_handle; session = link_session; generation = zoom_generation;
            motor_address = ronin_model_zoom_address(target_model);
        }
        portEXIT_CRITICAL(&lock);
        if(send)
        {
            uint8_t frame[18] = {0};
            if(duml_focus_motor(frame, sequence++, motor_address, position))
            {
                portENTER_CRITICAL(&lock);
                bool current = zoom_motion.active && zoom_available_locked(esp_timer_get_time()) &&
                               (link_session == session) && (zoom_session == session) && (zoom_generation == generation) &&
                               (conn == ch) && (write_handle == handle);
                portEXIT_CRITICAL(&lock);
                if(current && (ble_gattc_write_no_rsp_flat(ch, handle, frame, sizeof(frame)) != 0))
                {
                    portENTER_CRITICAL(&lock);
                    if((zoom_generation == generation) && (zoom_session == session))
                    { zoom_motion_cancel(&zoom_motion, ZOOM_ERROR_WRITE); }
                    portEXIT_CRITICAL(&lock);
                }
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}
