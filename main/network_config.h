#pragma once
#include <stdbool.h>
#include "esp_err.h"
/** @brief Start persistent station Wi-Fi and the USB provisioning service. */
esp_err_t network_config_start(void);
/** @brief Return whether the station is associated and holds an IPv4 address. */
bool network_config_connected(void);
