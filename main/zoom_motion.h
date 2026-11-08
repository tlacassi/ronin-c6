#pragma once
#include <stdbool.h>
#include <stdint.h>
#define ZOOM_POSITION_MAX 4095u
#define ZOOM_LEASE_MS 1500u

typedef enum
{
    ZOOM_ERROR_NONE = 0u,
    ZOOM_ERROR_UNAVAILABLE,
    ZOOM_ERROR_LEASE,
    ZOOM_ERROR_SCHEDULING,
    ZOOM_ERROR_WRITE
} zoom_error_t;
typedef struct
{
    uint16_t position;
    uint16_t target;
    uint8_t speed;
    uint32_t last_tick_ms;
    uint32_t lease_ms;
    uint32_t hold_ms;
    uint32_t remainder;
    bool active;
    bool holding;
    zoom_error_t error;
} zoom_motion_t;
/** @brief Start a bounded absolute trajectory; speed is percent of calibrated travel per second. */
bool zoom_motion_start(zoom_motion_t *motion, uint16_t position, uint16_t target, uint8_t speed, uint32_t now_ms);
/** @brief Renew an active trajectory's browser lease. */
void zoom_motion_renew(zoom_motion_t *motion, uint32_t now_ms);
/** @brief Cancel without queuing a future move or inventing a replacement position. */
void zoom_motion_cancel(zoom_motion_t *motion, zoom_error_t error);
/** @brief Produce one 100 Hz setpoint; stop on stale lease, lost motor, or scheduling stall. */
bool zoom_motion_tick(zoom_motion_t *motion, uint32_t now_ms, bool available, uint16_t *position);
