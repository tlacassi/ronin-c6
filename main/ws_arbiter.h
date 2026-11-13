#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Slot bookkeeping for /ws sockets: one controller at a time, the rest read-only
 * observers, plus heartbeat supervision. Pure logic, no ESP-IDF dependency; the
 * caller provides locking and the clock (microseconds). */
#define WS_MAX_CLIENTS 3u
#define WS_DEGRADED_US 2500000   /* silent controller: stop motion and zoom */
#define WS_DEAD_US 6000000       /* silent socket of any role: close it */

typedef struct
{
    int fd;
    bool used, controller, degraded, closing;
    uint32_t session;
    int64_t last_rx_us;
} ws_slot_t;

typedef struct { ws_slot_t slots[WS_MAX_CLIENTS]; } ws_arbiter_t;

typedef enum { WS_ROLE_UNKNOWN = 0, WS_ROLE_CONTROLLER, WS_ROLE_OBSERVER } ws_role_t;
typedef enum { WS_ACTION_NONE = 0, WS_ACTION_DEGRADE, WS_ACTION_CLOSE } ws_action_kind_t;
typedef struct { ws_action_kind_t kind; int fd; uint32_t session; } ws_action_t;

void ws_arbiter_init(ws_arbiter_t *arbiter);
/** @brief Register a new socket as an observer. False when full or already known. */
bool ws_arbiter_add(ws_arbiter_t *arbiter, int fd, int64_t now_us);
/** @brief Forget a socket. Returns true when it was the controller; its session goes to *session. */
bool ws_arbiter_remove(ws_arbiter_t *arbiter, int fd, uint32_t *session);
/** @brief Record any valid message from the socket (heartbeat). */
void ws_arbiter_touch(ws_arbiter_t *arbiter, int fd, int64_t now_us);
/** @brief Whether the socket is the active controller; its bound session goes to *session. */
bool ws_arbiter_controller(const ws_arbiter_t *arbiter, int fd, uint32_t *session);
/** @brief Claim control. A silent (degraded) controller can be replaced; its fd goes to *evicted (else -1). */
ws_role_t ws_arbiter_hello(ws_arbiter_t *arbiter, int fd, uint32_t session, int64_t now_us, int *evicted);
/** @brief Heartbeat supervision. Fills up to `capacity` actions and returns how many. */
unsigned ws_arbiter_poll(ws_arbiter_t *arbiter, int64_t now_us, ws_action_t *actions, unsigned capacity);
/** @brief Copy the open socket fds, returns the count. */
unsigned ws_arbiter_fds(const ws_arbiter_t *arbiter, int fds[WS_MAX_CLIENTS]);
