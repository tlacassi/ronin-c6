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
#include "nvs.h"
#include "sdkconfig.h"
#include "ronin_ble.h"
#include "motion_parse.h"
#include "preset_data.h"
#include "cJSON.h"
#include "mdns.h"
#include "network_config.h"

static const char *TAG = "ronin";
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t gamepad_js_start[] asm("_binary_gamepad_js_start");
extern const uint8_t gamepad_js_end[] asm("_binary_gamepad_js_end");
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
static const char *zoom_error_name(zoom_error_t error)
{
    switch(error)
    {
    case ZOOM_ERROR_NONE: return "";
    case ZOOM_ERROR_UNAVAILABLE: return "unavailable";
    case ZOOM_ERROR_LEASE: return "lease_expired";
    case ZOOM_ERROR_SCHEDULING: return "timing";
    case ZOOM_ERROR_WRITE: return "write_failed";
    default: return "unavailable";
    }
}
static esp_err_t status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char json[768] = {0};
    uint8_t battery = 0u;
    uint32_t session = 0u;
    bool ready = false;
    bool protocol_verified = ronin_ble_protocol_verified();
    bool control_enabled = ronin_ble_control_enabled();
    ronin_ble_link(&ready, &session);
    if(ronin_ble_battery(&battery))
    { snprintf(json, sizeof(json), "{\"ble\":%s,\"protocolVerified\":%s,\"controlEnabled\":%s,\"battery\":%u,\"session\":%" PRIu32 "}", ready ? "true" : "false", protocol_verified ? "true" : "false", control_enabled ? "true" : "false", (unsigned)battery, session); }
    else
    { snprintf(json, sizeof(json), "{\"ble\":%s,\"protocolVerified\":%s,\"controlEnabled\":%s,\"battery\":null,\"session\":%" PRIu32 "}", ready ? "true" : "false", protocol_verified ? "true" : "false", control_enabled ? "true" : "false", session); }
    int16_t pose[3] = {0};
    size_t used = strlen(json);
    if((used > 0u) && ronin_ble_pose(pose))
    { snprintf(json + used - 1u, sizeof(json) - used + 1u, ",\"pose\":[%d,%d,%d]}", pose[0], pose[1], pose[2]); }
    ronin_ble_diagnostics_t diagnostics = {0};
    ronin_ble_diagnostics(&diagnostics);
    used = strlen(json);
    if(used > 0u)
    {
        snprintf(json + used - 1u, sizeof(json) - used + 1u,
                 ",\"telemetry\":{\"subscribed\":%s,\"notifications\":%" PRIu32
                 ",\"frames\":%" PRIu32 ",\"poses\":%" PRIu32 ",\"poseAgeMs\":%" PRId32 ",\"disconnectReason\":%" PRId32 "}}",
                 diagnostics.subscribed ? "true" : "false", diagnostics.notifications,
                 diagnostics.frames, diagnostics.poses, diagnostics.pose_age_ms, diagnostics.disconnect_reason);
    }
    ronin_zoom_status_t zoom = {0};
    ronin_ble_zoom_status(&zoom);
    used = strlen(json);
    if(used > 0u)
    {
        char position[8] = "null";
        if(zoom.calibrated) { snprintf(position, sizeof(position), "%u", (unsigned)zoom.position); }
        snprintf(json + used - 1u, sizeof(json) - used + 1u,
                 ",\"zoom\":{\"present\":%s,\"calibrated\":%s,\"position\":%s,"
                 "\"available\":%s,\"moving\":%s,\"target\":%u,\"error\":\"%s\"}}",
                 zoom.present ? "true" : "false", zoom.calibrated ? "true" : "false", position,
                 zoom.available ? "true" : "false", zoom.moving ? "true" : "false",
                 (unsigned)zoom.target, zoom_error_name(zoom.error));
    }
    bool sleeping = false;
    bool sleep_known = ronin_ble_sleep_state(&sleeping);
    used = strlen(json);
    if(used > 0u)
    {
        snprintf(json + used - 1u, sizeof(json) - used + 1u, ",\"sleeping\":%s}",
                 sleep_known ? (sleeping ? "true" : "false") : "null");
    }
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
static const char *model_name(ronin_model_t model)
{
    switch(model)
    {
    case RONIN_MODEL_RS3: return "RS 3";
    case RONIN_MODEL_RS4: return "RS 4";
    case RONIN_MODEL_RS4_PRO: return "RS 4 Pro";
    case RONIN_MODEL_RS5: return "RS 5";
    default: return "Inconnu";
    }
}
static esp_err_t devices(httpd_req_t *req)
{
    ronin_ble_device_t devices[RONIN_SCAN_MAX] = {0};
    char target_id[RONIN_DEVICE_ID_MAX] = {0};
    char target_name[RONIN_NAME_MAX + 1u] = {0};
    bool connected = false, ready = false;
    size_t count = ronin_ble_devices(devices);
    ronin_ble_target(target_id, target_name, &connected, &ready);
    bool protocol_verified = ronin_ble_protocol_verified();
    bool control_enabled = ronin_ble_control_enabled();
    cJSON *root = cJSON_CreateObject();
    if(root == NULL) { return ESP_FAIL; }
    cJSON *list = cJSON_AddArrayToObject(root, "devices");
    if((list == NULL) || (cJSON_AddStringToObject(root, "targetId", target_id) == NULL) ||
       (cJSON_AddStringToObject(root, "targetName", target_name) == NULL) ||
       (cJSON_AddBoolToObject(root, "connected", connected) == NULL) ||
       (cJSON_AddBoolToObject(root, "ready", ready) == NULL) ||
       (cJSON_AddBoolToObject(root, "protocolVerified", protocol_verified) == NULL) ||
       (cJSON_AddBoolToObject(root, "controlEnabled", control_enabled) == NULL))
    { cJSON_Delete(root); return ESP_FAIL; }
    for(size_t i = 0u; i < count; i++)
    {
        char id[RONIN_DEVICE_ID_MAX] = {0};
        ronin_ble_device_id(&devices[i], id);
        cJSON *entry = cJSON_CreateObject();
        if((entry == NULL) || (cJSON_AddStringToObject(entry, "id", id) == NULL) ||
           (cJSON_AddStringToObject(entry, "name", devices[i].name) == NULL) ||
           (cJSON_AddStringToObject(entry, "model", model_name(devices[i].model)) == NULL) ||
           (cJSON_AddNumberToObject(entry, "rssi", devices[i].rssi) == NULL) ||
           (cJSON_AddBoolToObject(entry, "visible", devices[i].visible) == NULL) ||
           (cJSON_AddBoolToObject(entry, "target", devices[i].is_target) == NULL) ||
           (cJSON_AddBoolToObject(entry, "connected", devices[i].is_connected) == NULL))
        {
            cJSON_Delete(entry);
            cJSON_Delete(root);
            return ESP_FAIL;
        }
        cJSON_AddItemToArray(list, entry);
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
    char id[RONIN_DEVICE_ID_MAX] = {0};
    if((req->content_len != (RONIN_DEVICE_ID_MAX - 1u)))
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid device identity"); return ESP_FAIL; }
    int total = 0;
    while(total < req->content_len)
    {
        int n = httpd_req_recv(req, id + total, req->content_len - total);
        if(n <= 0) { return ESP_FAIL; }
        total += n;
    }
    if(memchr(id, '\0', (size_t)total) != NULL)
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid device identity"); return ESP_FAIL; }
    if(!ronin_ble_select(id))
    { httpd_resp_set_status(req, "409 Conflict"); return httpd_resp_sendstr(req, "Target no longer visible"); }
    return httpd_resp_sendstr(req, "Target saved; switching BLE connection");
}
static esp_err_t disconnect_device(httpd_req_t *req)
{
    (void)req;
    if(!ronin_ble_forget())
    { httpd_resp_set_status(req, "500 Internal Server Error"); return httpd_resp_sendstr(req, "Could not clear saved target"); }
    return httpd_resp_sendstr(req, "Disconnected; target forgotten");
}
static esp_err_t discover_devices(httpd_req_t *req)
{
    (void)req;
    ronin_ble_begin_discovery();
    return httpd_resp_sendstr(req, "Scanning for Ronin devices");
}
static esp_err_t resume_target(httpd_req_t *req)
{
    (void)req;
    ronin_ble_resume_target();
    return httpd_resp_sendstr(req, "Resuming saved target connection");
}
_Static_assert(RONIN_NAME_MAX == PRESET_NAME_MAX, "Preset names must match the BLE name limit");
static uint32_t preset_magic(void)
{
    /* Old Pro presets used legacy tilt/roll fields and must be recorded again. */
    return ronin_ble_selected_model() == RONIN_MODEL_RS4_PRO ? 0x52535033u : 0x52535031u;
}
static bool preset_load(unsigned index, preset_t *preset)
{
    nvs_handle_t store; char key[8]; snprintf(key, sizeof(key), "p%u", index);
    if(nvs_open("presets", NVS_READONLY, &store) != ESP_OK) { return false; }
    uint8_t data[sizeof(preset_t)] = {0};
    size_t size = sizeof(data);
    esp_err_t err = nvs_get_blob(store, key, data, &size); nvs_close(store);
    return (err == ESP_OK) && preset_decode(data, size, preset_magic(), preset);
}
static esp_err_t presets_get(httpd_req_t *req)
{
    char selected[RONIN_NAME_MAX+1u] = {0}; ronin_ble_selected(selected);
    cJSON *root = cJSON_CreateArray(); if(root == NULL) { return ESP_FAIL; }
    for(unsigned i=0u; i<5u; i++)
    {
        preset_t preset = {0};
        if(preset_load(i, &preset) && strcmp(preset.axes.name, selected) == 0)
        {
            cJSON *entry = cJSON_CreateIntArray((int[]){preset.axes.pose[0],preset.axes.pose[1],preset.axes.pose[2]},3);
            if(entry == NULL) { cJSON_Delete(root); return ESP_FAIL; } cJSON_AddItemToArray(root,entry);
        }
        else { cJSON_AddItemToArray(root,cJSON_CreateNull()); }
    }
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root); if(body == NULL) { return ESP_FAIL; }
    httpd_resp_set_type(req,"application/json"); httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t rc = httpd_resp_sendstr(req,body); free(body); return rc;
}
static esp_err_t zoom_presets_get(httpd_req_t *req)
{
    char selected[RONIN_NAME_MAX + 1u] = {0}; ronin_ble_selected(selected);
    cJSON *root = cJSON_CreateArray();
    if(root == NULL) { return ESP_FAIL; }
    for(unsigned i = 0u; i < 5u; i++)
    {
        preset_t preset = {0};
        bool valid = preset_load(i, &preset) && (strcmp(preset.axes.name, selected) == 0) && preset.zoom_valid;
        cJSON *entry = valid ? cJSON_CreateNumber(preset.zoom_position) : cJSON_CreateNull();
        if(entry == NULL) { cJSON_Delete(root); return ESP_FAIL; }
        cJSON_AddItemToArray(root, entry);
    }
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if(body == NULL) { return ESP_FAIL; }
    httpd_resp_set_type(req, "application/json"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, body); free(body); return result;
}
static esp_err_t zoom_action(httpd_req_t *req)
{
    uint32_t session = 0u;
    if(!read_session(req, &session))
    { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session"); return ESP_FAIL; }
    bool accepted = false;
    if(strcmp(req->uri, "/zoom") == 0)
    {
        char body[9] = {0}; int received = 0;
        if((req->content_len < 3) || (req->content_len > 8))
        { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid zoom request"); return ESP_FAIL; }
        while(received < req->content_len)
        {
            int count = httpd_req_recv(req, body + received, req->content_len - received);
            if(count <= 0) { return ESP_FAIL; }
            received += count;
        }
        uint16_t position = 0u; uint8_t speed = 0u;
        if(!zoom_parse(body, (size_t)received, &position, &speed))
        { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid zoom request"); return ESP_FAIL; }
        accepted = ronin_ble_zoom_goto(session, position, speed);
    }
    else
    {
        if(req->content_len != 0)
        { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body must be empty"); return ESP_FAIL; }
        accepted = strcmp(req->uri, "/zoom/stop") == 0 ?
                   ronin_ble_zoom_stop(session) : ronin_ble_zoom_keepalive(session);
    }
    if(!accepted)
    { httpd_resp_set_status(req, "503 Service Unavailable"); return httpd_resp_sendstr(req, "Zoom or session unavailable"); }
    return httpd_resp_sendstr(req, "OK");
}
static esp_err_t preset_action(httpd_req_t *req)
{
    uint32_t session=0u; char body[9]={0};
    bool is_goto = strcmp(req->uri,"/preset/goto") == 0;
    int expected = is_goto && (req->content_len == 8) ? 8 : is_goto ? 4 : 1;
    if(!read_session(req,&session) || req->content_len != expected)
    { httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request"); return ESP_FAIL; }
    int received = 0;
    while(received < expected)
    {
        int n = httpd_req_recv(req, body + received, expected - received);
        if(n <= 0) { return ESP_FAIL; }
        received += n;
    }
    if(body[0] < '0' || body[0] > '4' || (is_goto && (body[1] != ',' || body[2] < '0' || body[2] > '9' || body[3] < '0' || body[3] > '9')))
    { httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request"); return ESP_FAIL; }
    uint8_t zoom_speed = 20u;
    if(is_goto && (expected == 8))
    {
        if((body[4] != ',') || (body[5] < '0') || (body[5] > '9') ||
           (body[6] < '0') || (body[6] > '9') || (body[7] < '0') || (body[7] > '9'))
        { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid zoom speed"); return ESP_FAIL; }
        unsigned speed = (unsigned)(body[5] - '0') * 100u + (unsigned)(body[6] - '0') * 10u + (unsigned)(body[7] - '0');
        if((speed < 1u) || (speed > 100u))
        { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid zoom speed"); return ESP_FAIL; }
        zoom_speed = (uint8_t)speed;
    }
    unsigned index=(unsigned)(body[0]-'0'); bool ready=false; uint32_t current=0u; ronin_ble_link(&ready,&current);
    if(!ready || current != session) { httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Link unavailable"); return ESP_FAIL; }
    preset_t preset={0}; char selected[RONIN_NAME_MAX+1u]={0}; ronin_ble_selected(selected);
    if(strcmp(req->uri,"/preset/save") == 0)
    {
        if(!ronin_ble_pose(preset.axes.pose) || selected[0] == 0) { httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Fresh pose unavailable"); return ESP_FAIL; }
        preset.axes.magic=preset_magic(); strlcpy(preset.axes.name,selected,sizeof(preset.axes.name));
        ronin_zoom_status_t zoom = {0}; ronin_ble_zoom_status(&zoom);
        if(zoom.available)
        {
            preset.zoom_valid = 1u; preset.zoom_motor = 0xbfu; preset.zoom_position = zoom.position;
        }
        char key[8]; snprintf(key,sizeof(key),"p%u",index); nvs_handle_t store;
        if(nvs_open("presets",NVS_READWRITE,&store) != ESP_OK) { httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage error"); return ESP_FAIL; }
        esp_err_t err=nvs_set_blob(store,key,&preset,sizeof(preset)); if(err == ESP_OK) { err=nvs_commit(store); } nvs_close(store);
        if(err != ESP_OK) { httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage error"); return ESP_FAIL; }
    }
    else if(strcmp(req->uri,"/preset/delete") == 0)
    {
        if(!preset_load(index,&preset) || strcmp(preset.axes.name,selected)!=0) { httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Preset unavailable"); return ESP_FAIL; }
        char key[8]; snprintf(key,sizeof(key),"p%u",index); nvs_handle_t store;
        if(nvs_open("presets",NVS_READWRITE,&store) != ESP_OK) { httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage error"); return ESP_FAIL; }
        esp_err_t err=nvs_erase_key(store,key); if(err == ESP_OK) { err=nvs_commit(store); } nvs_close(store);
        if(err != ESP_OK) { httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage error"); return ESP_FAIL; }
    }
    else
    {
        uint8_t duration=(uint8_t)((body[2]-'0')*10 + body[3]-'0');
        if(!preset_load(index,&preset) || strcmp(preset.axes.name,selected)!=0 || preset.axes.pose[0]<-1800 || preset.axes.pose[0]>1800 || preset.axes.pose[1]<-1800 || preset.axes.pose[1]>1800 || preset.axes.pose[2]<-1800 || preset.axes.pose[2]>1800 || !ronin_ble_goto(session,preset.axes.pose,duration))
        { httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Preset or link unavailable"); return ESP_FAIL; }
        if(preset.zoom_valid)
        {
            /* A missing or uncalibrated optional motor must not block the axes. */
            (void)ronin_ble_zoom_goto(session, preset.zoom_position, zoom_speed);
        }
    }
    return httpd_resp_sendstr(req,"OK");
}
static esp_err_t home_action(httpd_req_t *req)
{
    uint32_t l_u32Session = 0u;
    const int16_t l_ai16Pose[3] = {0, 0, 0};
    if((req->content_len != 0) || !read_session(req, &l_u32Session))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid home request");
        return ESP_FAIL;
    }
    if(!ronin_ble_move(0, 0, l_u32Session) || !ronin_ble_goto(l_u32Session, l_ai16Pose, 20u))
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "Fresh pose or link unavailable");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "OK");
}
static esp_err_t sleep_action(httpd_req_t *req)
{
    uint32_t session = 0u;
    char action = 0;
    if((req->content_len != 1) || !read_session(req, &session) ||
       (httpd_req_recv(req, &action, 1u) != 1) || ((action != '0') && (action != '1')))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid sleep request");
        return ESP_FAIL;
    }
    if(!ronin_ble_set_sleep(session, action == '1'))
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "State or link unavailable; stop motion first");
    }
    return httpd_resp_sendstr(req, "Sent");
}
static void start_server(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    /* Browser polling can retain every socket across multiple open clients. */
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 1;
    cfg.max_uri_handlers = 19;
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    const httpd_uri_t zoom_presets_uri = {.uri="/zoom/presets", .method=HTTP_GET, .handler=zoom_presets_get};
    const httpd_uri_t zoom_uri = {.uri="/zoom", .method=HTTP_POST, .handler=zoom_action};
    const httpd_uri_t zoom_stop_uri = {.uri="/zoom/stop", .method=HTTP_POST, .handler=zoom_action};
    const httpd_uri_t zoom_keep_uri = {.uri="/zoom/keepalive", .method=HTTP_POST, .handler=zoom_action};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &zoom_presets_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &zoom_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &zoom_stop_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &zoom_keep_uri));
    const httpd_uri_t home_uri = {.uri="/home", .method=HTTP_POST, .handler=home_action};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &home_uri));
    const httpd_uri_t sleep_uri = {.uri="/sleep", .method=HTTP_POST, .handler=sleep_action};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &sleep_uri));
    const httpd_uri_t page_uri = {.uri="/", .method=HTTP_GET, .handler=page};
    const httpd_uri_t script_uri = {.uri="/gamepad.js", .method=HTTP_GET, .handler=gamepad_script};
    const httpd_uri_t status_uri = {.uri="/status", .method=HTTP_GET, .handler=status};
    const httpd_uri_t devices_uri = {.uri="/devices", .method=HTTP_GET, .handler=devices};
    const httpd_uri_t select_uri = {.uri="/select", .method=HTTP_POST, .handler=select_device};
    const httpd_uri_t disconnect_uri = {.uri="/disconnect", .method=HTTP_POST, .handler=disconnect_device};
    const httpd_uri_t discover_uri = {.uri="/discover", .method=HTTP_POST, .handler=discover_devices};
    const httpd_uri_t resume_uri = {.uri="/resume", .method=HTTP_POST, .handler=resume_target};
    const httpd_uri_t presets_uri = {.uri="/presets", .method=HTTP_GET, .handler=presets_get};
    const httpd_uri_t save_uri = {.uri="/preset/save", .method=HTTP_POST, .handler=preset_action};
    const httpd_uri_t goto_uri = {.uri="/preset/goto", .method=HTTP_POST, .handler=preset_action};
    const httpd_uri_t delete_uri = {.uri="/preset/delete", .method=HTTP_POST, .handler=preset_action};
    const httpd_uri_t move_uri = {.uri="/move", .method=HTTP_POST, .handler=move};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &page_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &status_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &script_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &move_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &presets_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &save_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &goto_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &delete_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &devices_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &select_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &disconnect_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &discover_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &resume_uri));
}
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    { ESP_LOGE(TAG, "NVS requires maintenance; refusing to erase it automatically"); return; }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(network_config_start());
    start_server();
    esp_err_t mdns_result = mdns_init();
    if(mdns_result == ESP_OK) { mdns_result = mdns_hostname_set("ronin-c6"); }
    if(mdns_result == ESP_OK) { mdns_result = mdns_service_add("Ronin C6", "_http", "_tcp", 80, NULL, 0); }
    if(mdns_result != ESP_OK) { ESP_LOGW(TAG, "mDNS unavailable: %s", esp_err_to_name(mdns_result)); }
    else { ESP_LOGI(TAG, "Open http://ronin-c6.local/"); }
    ronin_ble_start();
}
