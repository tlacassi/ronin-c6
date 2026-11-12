#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "zoom_motion.h"
#define RONIN_NAME_MAX 31u
#define RONIN_SCAN_MAX 12u
#define RONIN_DEVICE_ID_MAX 21u
typedef enum
{
    RONIN_MODEL_UNKNOWN = 0u,
    RONIN_MODEL_RS3 = 3u,
    RONIN_MODEL_RS4 = 4u,
    RONIN_MODEL_RS4_PRO = 40u,
    RONIN_MODEL_RS5 = 5u
} ronin_model_t;
/** @brief Return the captured DUML address for this model's single zoom motor. */
static inline uint8_t ronin_model_zoom_address(ronin_model_t model)
{
    if(model == RONIN_MODEL_RS3) { return 0xdfu; }
    if(model == RONIN_MODEL_RS4_PRO) { return 0xbfu; }
    return 0u;
}
/** @brief Return whether this model can expose the supported single zoom motor. */
static inline bool ronin_model_supports_zoom(ronin_model_t model)
{
    return ronin_model_zoom_address(model) != 0u;
}
typedef struct
{
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    ronin_model_t model;
    bool visible;
    bool is_target;
    bool is_connected;
    char name[RONIN_NAME_MAX + 1u];
} ronin_ble_device_t;
/** @brief Copy currently visible gimbals, the saved target and active connection. */
size_t ronin_ble_devices(ronin_ble_device_t devices[RONIN_SCAN_MAX]);
/** @brief Format a stable address-based identity for the web API. */
void ronin_ble_device_id(const ronin_ble_device_t *device, char id[RONIN_DEVICE_ID_MAX]);
/** @brief Select a discovered identity, persist it and switch connections if needed. */
bool ronin_ble_select(const char *device_id);
/** @brief Clear the saved target and disconnect without stopping discovery. */
bool ronin_ble_forget(void);
/** @brief Pause the saved target connection and scan for alternative gimbals. */
void ronin_ble_begin_discovery(void);
/** @brief Resume automatic connection to the saved target after discovery. */
void ronin_ble_resume_target(void);
/** @brief Copy the selected name into a caller-owned buffer. */
void ronin_ble_selected(char name[RONIN_NAME_MAX + 1u]);
/** @brief Read saved target identity/name and whether it is connected and ready. */
void ronin_ble_target(char id[RONIN_DEVICE_ID_MAX], char name[RONIN_NAME_MAX + 1u], bool *connected, bool *ready);
/** @brief Return whether motion commands are verified for the selected model. */
bool ronin_ble_protocol_verified(void);
/** @brief Return whether manual control is enabled for the selected model. */
bool ronin_ble_control_enabled(void);
/** @brief Return the advertised model associated with the selected target. */
ronin_model_t ronin_ble_selected_model(void);
/** @brief Return the latest supported battery value if still fresh. */
bool ronin_ble_battery(uint8_t *value);
/** @brief Return the fresh sleep state for the connected RS 3. */
bool ronin_ble_sleep_state(bool *sleeping);
/** @brief Request RS 3 sleep or wake for the current connection. */
bool ronin_ble_set_sleep(uint32_t session, bool sleep);
/** @brief Copy a fresh pose in tenths of a degree. */
bool ronin_ble_pose(int16_t pose[3]);
/** @brief Start the BLE central and background control task. */
void ronin_ble_start(void);
/** @brief Return whether the command characteristic has been discovered. */
bool ronin_ble_ready(void);
/** @brief Set the requested motion; commands expire unless refreshed. */
bool ronin_ble_move(int16_t yaw, int16_t pitch, uint32_t session);
/** @brief Obtain an atomic link snapshot for command freshness checks. */
void ronin_ble_link(bool *ready, uint32_t *session);
/** @brief Stop motion and invalidate commands issued before a network loss. */
void ronin_ble_invalidate(void);
/** @brief Clear any pending motion. */
void ronin_ble_stop(void);

/** @brief Send an experimental absolute pose when link and pose are fresh. */
bool ronin_ble_goto(uint32_t session, const int16_t pose[3], uint8_t duration);

typedef struct
{
    uint32_t notifications;
    uint32_t frames;
    uint32_t poses;
    int32_t pose_age_ms;
    int32_t disconnect_reason;
    bool subscribed;
} ronin_ble_diagnostics_t;
/** @brief Copy receive counters and pose age for the current BLE connection. */
void ronin_ble_diagnostics(ronin_ble_diagnostics_t *diagnostics);

typedef struct
{
    uint16_t position;
    uint16_t target;
    bool present;
    bool calibrated;
    bool available;
    bool moving;
    zoom_error_t error;
} ronin_zoom_status_t;
/** @brief Read fresh state of the motor used as zoom; position is valid only when calibrated. */
void ronin_ble_zoom_status(ronin_zoom_status_t *status);
/** @brief Start a leased zoom trajectory on a fresh, calibrated motor. */
bool ronin_ble_zoom_goto(uint32_t session, uint16_t target, uint8_t speed);
/** @brief Renew zoom activity from the controlling page. */
bool ronin_ble_zoom_keepalive(uint32_t session);
/** @brief Cancel queued zoom setpoints for the current session. */
bool ronin_ble_zoom_stop(uint32_t session);
