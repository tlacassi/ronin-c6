#pragma once
#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"

/** @brief Fills the GET /status JSON body into buffer and returns its length. */
typedef size_t (*ws_status_fn)(char *buffer, size_t capacity);

/** @brief httpd_config_t.close_fn: runs on every socket close (client close, error, LRU purge) and stops motion if the controller left. */
void ws_control_on_close(httpd_handle_t server, int fd);
/** @brief Register GET /ws and start the state push / heartbeat task. */
esp_err_t ws_control_start(httpd_handle_t server, ws_status_fn status);
