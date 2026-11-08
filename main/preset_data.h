#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PRESET_NAME_MAX 31u
typedef struct
{
    uint32_t magic;
    int16_t pose[3];
    char name[PRESET_NAME_MAX + 1u];
} preset_axes_t;
typedef struct
{
    preset_axes_t axes;
    uint16_t zoom_position;
    uint8_t zoom_valid;
    uint8_t zoom_motor;
} preset_t;
/** @brief Decode legacy axes-only or extended presets without altering stored data. */
bool preset_decode(const void *data, size_t length, uint32_t magic, preset_t *preset);
