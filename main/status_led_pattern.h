#pragma once
#include <stdint.h>
typedef enum
{
    STATUS_LED_BOOT = 0,  /* white, double flash */
    STATUS_LED_NO_WIFI,   /* red, blinking */
    STATUS_LED_NO_RONIN,  /* blue, blinking */
    STATUS_LED_OK         /* green, breathing */
} status_led_state_t;
/** @brief Full-scale RGB (0-255) for a state at a time offset in milliseconds. Pure function. */
void status_led_pattern(status_led_state_t state, uint32_t time_ms, uint8_t rgb[3]);
