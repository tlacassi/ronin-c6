#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TELEMETRY_FRAME_MAX 128u
typedef enum
{
    TELEMETRY_POSE_NONE = 0u,
    TELEMETRY_POSE_TLV = 1u,
    TELEMETRY_POSE_PARAMS49 = 2u
} telemetry_pose_format_t;
typedef struct
{
    uint8_t frame[TELEMETRY_FRAME_MAX];
    size_t used;
    size_t expected;
    int16_t pose[3];
    telemetry_pose_format_t pose_format;
    uint8_t focus_address;
    uint16_t focus_position;
    bool focus_calibrated;
    bool focus_updated;
    bool pose_updated;
    bool frame_valid;
} telemetry_parser_t;
/** @brief Consume one byte; report when a supported battery frame completes. */
bool telemetry_feed(telemetry_parser_t *parser, uint8_t byte, uint8_t *battery);
