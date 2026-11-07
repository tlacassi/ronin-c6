#pragma once
#include <stdint.h>
#include <stddef.h>
/** @brief Encode the RS 3 velocity command in a 20-byte DUML frame. */
void duml_speed(uint8_t out[20], uint16_t seq, int16_t yaw, int16_t roll, int16_t pitch);
/** @brief Calculate the DUML header checksum. */
uint8_t duml_crc8(const uint8_t *data, size_t len);
/** @brief Calculate the DUML frame checksum. */
uint16_t duml_crc16(const uint8_t *data, size_t len);
