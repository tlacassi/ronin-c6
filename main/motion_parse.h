#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** @brief Parse a bounded ASCII yaw,pitch command. */
bool motion_parse(const char *data, size_t len, int16_t *yaw, int16_t *pitch);

/** @brief Parse a zoom target (0..4095) and speed (1..100 percent travel per second). */
bool zoom_parse(const char *data, size_t len, uint16_t *position, uint8_t *speed);
