#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** @brief Parse a bounded ASCII yaw,pitch command. */
bool motion_parse(const char *data, size_t len, int16_t *yaw, int16_t *pitch);
