#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TELEMETRY_FRAME_MAX 128u
typedef struct
{
    uint8_t frame[TELEMETRY_FRAME_MAX];
    size_t used;
    size_t expected;
    int16_t pose[3];
    bool pose_updated;
} telemetry_parser_t;
/** @brief Consume one byte; report an experimental RS 3 Mini-compatible battery value. */
bool telemetry_feed(telemetry_parser_t *parser, uint8_t byte, uint8_t *battery);
