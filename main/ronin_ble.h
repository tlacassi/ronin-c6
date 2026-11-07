#pragma once
#include <stdbool.h>
#include <stdint.h>
/** @brief Start the BLE central and background control task. */
void ronin_ble_start(void);
/** @brief Return whether the command characteristic has been discovered. */
bool ronin_ble_ready(void);
/** @brief Set the requested motion; commands expire unless refreshed. */
bool ronin_ble_move(int16_t yaw, int16_t pitch);
/** @brief Clear any pending motion. */
void ronin_ble_stop(void);
