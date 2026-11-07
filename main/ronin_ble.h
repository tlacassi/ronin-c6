#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define RONIN_NAME_MAX 31u
#define RONIN_SCAN_MAX 8u
/** @brief Copy the recently observed DJI names. */
size_t ronin_ble_devices(char names[RONIN_SCAN_MAX][RONIN_NAME_MAX + 1u]);
/** @brief Select a discovered name and persist it when no connection is active. */
bool ronin_ble_select(const char *name);
/** @brief Copy the selected name into a caller-owned buffer. */
void ronin_ble_selected(char name[RONIN_NAME_MAX + 1u]);
/** @brief Return the latest experimental battery value if still fresh. */
bool ronin_ble_battery(uint8_t *value);
/** @brief Copy a fresh pose in tenths of a degree. */
bool ronin_ble_pose(int16_t pose[3]);
/** @brief Start the BLE central and background control task. */
void ronin_ble_start(void);
/** @brief Return whether the command characteristic has been discovered. */
bool ronin_ble_ready(void);
/** @brief Set the requested motion; commands expire unless refreshed. */
bool ronin_ble_move(int16_t yaw, int16_t pitch, uint32_t session);
/** @brief Obtain an atomic link snapshot for command freshness checks. */
void ronin_ble_link(bool *ready, uint32_t *session);
/** @brief Stop motion and invalidate commands issued before a network loss. */
void ronin_ble_invalidate(void);
/** @brief Clear any pending motion. */
void ronin_ble_stop(void);

/** @brief Send an experimental absolute pose when link and pose are fresh. */
bool ronin_ble_goto(uint32_t session, const int16_t pose[3], uint8_t duration);
