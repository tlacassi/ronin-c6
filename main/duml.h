#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/** @brief Encode the captured RS 3 sleep or wake command. */
void duml_sleep(uint8_t out[16], uint16_t seq, bool sleep);
/** @brief Encode the RS 3 velocity command in a 20-byte DUML frame. */
void duml_speed(uint8_t out[20], uint16_t seq, int16_t yaw, int16_t roll, int16_t pitch);
/** @brief Calculate the DUML header checksum. */
uint8_t duml_crc8(const uint8_t *data, size_t len);
/** @brief Calculate the DUML frame checksum. */
uint16_t duml_crc16(const uint8_t *data, size_t len);

#define DUML_STATUS_POLL_MAX 36u
/** @brief Encode one of the three captured RS 3 app status poll stages (0..2).
 *  @return Frame length, or zero for invalid arguments.
 */
size_t duml_status_poll(uint8_t out[DUML_STATUS_POLL_MAX], uint16_t seq, unsigned stage);

/** @brief Encode an angle command, with yaw/roll/pitch in signed tenths of a degree. */
void duml_angle(uint8_t out[21], uint16_t seq, int16_t yaw, int16_t roll, int16_t pitch, uint8_t flags, uint8_t duration);

/** @brief Encode the observed motor's absolute position command; reject values outside 0..4095. */
bool duml_focus(uint8_t out[18], uint16_t seq, uint16_t position);
/** @brief Encode a focus position for the captured RS 3 (DF) or RS 4 Pro (BF) motor address. */
bool duml_focus_motor(uint8_t out[18], uint16_t seq, uint8_t address, uint16_t position);
