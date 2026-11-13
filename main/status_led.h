#pragma once
#include <stdbool.h>
#include <stdint.h>
/** @brief Start the on-board RGB LED status task (no-op when the GPIO is disabled). Requires NVS. */
void status_led_start(void);
/** @brief Current LED brightness in percent; 0 means the LED is off. */
uint8_t status_led_brightness(void);
/** @brief Set and persist the LED brightness (0-100). Returns false when out of range or not saved. */
bool status_led_set_brightness(uint8_t percent);
