#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compact text protocol for the /ws control socket. No ESP-IDF dependency.
 *
 * One WebSocket text frame is one message: a single type letter followed by a
 * payload that reuses the strict HTTP body parsers (same ranges and limits).
 *   client -> ESP   h<session>        hello, binds this socket to a BLE link session
 *                   m<yaw>,<pitch>    move, +-600 (motion_parse)
 *                   z<pos>,<speed>    zoom goto, 0-4095 / 1-100 (zoom_parse)
 *                   x                 zoom stop
 *                   k                 zoom keepalive
 *                   p<ms>             ping, echoed back for round-trip timing
 *   ESP -> client   s<json>           state, identical to the GET /status body
 *                   o<ms>             pong
 *                   r<1|0>            role granted: 1 controller, 0 read-only observer
 *                   e<code>           error: bad, role, sess, link, zoom, busy
 */
#define WS_MESSAGE_MAX 64u

typedef enum
{
    WS_MSG_NONE = 0,
    WS_MSG_HELLO,
    WS_MSG_MOVE,
    WS_MSG_ZOOM,
    WS_MSG_ZOOM_STOP,
    WS_MSG_KEEPALIVE,
    WS_MSG_PING
} ws_message_type_t;

typedef struct
{
    ws_message_type_t type;
    uint32_t session;       /* HELLO */
    int16_t yaw, pitch;     /* MOVE */
    uint16_t zoom_position; /* ZOOM */
    uint8_t zoom_speed;     /* ZOOM */
    uint32_t timestamp;     /* PING */
} ws_message_t;

typedef enum
{
    WS_ERR_BAD = 0,  /* malformed, out of range or oversized */
    WS_ERR_ROLE,     /* command from a socket that is not the controller */
    WS_ERR_SESSION,  /* session differs from the current BLE link session */
    WS_ERR_LINK,     /* link not ready or command refused */
    WS_ERR_ZOOM,     /* zoom unavailable or lease refused */
    WS_ERR_BUSY      /* no free control slot */
} ws_error_t;

/** @brief Strictly parse one frame payload. Rejects anything malformed, out of range or too long. */
bool ws_parse(const char *data, size_t length, ws_message_t *out);
/** @brief Format "o<ms>". Returns the length written, 0 if it does not fit. */
size_t ws_format_pong(char *out, size_t capacity, uint32_t timestamp);
/** @brief Format "e<code>". */
size_t ws_format_error(char *out, size_t capacity, ws_error_t code);
/** @brief Format "r1" or "r0". */
size_t ws_format_role(char *out, size_t capacity, bool controller);
/** @brief Cross-site WebSocket hijacking guard: Origin must be http://<same authority as Host>. A missing Origin is rejected. */
bool ws_origin_allowed(const char *origin, const char *host);
