#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "telemetry.h"
#include "duml.h"
static unsigned feed(telemetry_parser_t *parser, const uint8_t *data, size_t length, uint8_t *battery)
{
    unsigned count = 0u;
    for(size_t i = 0; i < length; i++) { if(telemetry_feed(parser, data[i], battery)) { count++; } }
    return count;
}
int main(void)
{
    /* The frame below was captured on an RS 3; the battery interpretation is inferred from RS 3 Mini. */
    const uint8_t frame[] = {0x55,0x22,0x04,0xea,0xe5,0x02,0x98,0x1d,0x00,0x0d,0x02,0x00,0x00,0x20,0x00,0x00,0x23,0x06,0x00,0x00,0x40,0x0a,0x00,0x00,0xf1,0x09,0x00,0x00,0x8d,0x01,0x04,0x61,0xc2,0xf3};
    telemetry_parser_t parser = {0}; uint8_t battery = 0u;
    const uint8_t asleep[] = {0x55,0x12,0x04,0xc7,0x04,0x02,0x56,0x0a,0x00,0x04,0x27,0x00,0x00,0x00,0x00,0x01,0xec,0xed};
    const uint8_t awake[] = {0x55,0x12,0x04,0xc7,0x04,0x02,0x5c,0x0a,0x00,0x04,0x27,0x00,0x00,0x00,0x00,0x00,0x02,0xcd};
    feed(&parser, asleep, sizeof(asleep), &battery);
    assert(parser.frame_valid && parser.sleep_updated && parser.sleeping);
    feed(&parser, awake, sizeof(awake), &battery);
    assert(parser.frame_valid && parser.sleep_updated && !parser.sleeping);
    uint8_t bad_sleep[sizeof(asleep)]; memcpy(bad_sleep, asleep, sizeof(bad_sleep)); bad_sleep[15] ^= 1u;
    feed(&parser, bad_sleep, sizeof(bad_sleep), &battery);
    assert(!parser.frame_valid && !parser.sleep_updated);
    memcpy(bad_sleep, asleep, sizeof(bad_sleep)); bad_sleep[15] = 2u;
    uint16_t sleep_crc = duml_crc16(bad_sleep, sizeof(bad_sleep) - 2u);
    bad_sleep[16] = (uint8_t)sleep_crc; bad_sleep[17] = (uint8_t)(sleep_crc >> 8u);
    feed(&parser, bad_sleep, sizeof(bad_sleep), &battery);
    assert(parser.frame_valid && !parser.sleep_updated);
    assert(feed(&parser, frame, 12u, &battery) == 0u);
    assert(feed(&parser, frame + 12u, sizeof(frame) - 12u, &battery) == 1u);
    assert(battery == 97u);
    uint8_t damaged[sizeof(frame)]; memcpy(damaged, frame, sizeof(frame)); damaged[31] ^= 1u;
    assert(feed(&parser, damaged, sizeof(damaged), &battery) == 0u);
    assert(feed(&parser, frame, sizeof(frame), &battery) == 1u);
    assert(feed(&parser, frame, sizeof(frame), &battery) == 1u);
    /* Complete RS 3 angle packet captured on the physical gimbal. */
    const uint8_t pose_frame[] = {0x55,0x2c,0x04,0x36,0xe5,0x02,0xf0,0x1b,0x00,0x04,0x66,0x01,0x06,0x01,0x01,0x07,0x01,0xfe,0x08,0x01,0x00,0x0a,0x01,0x00,0x0b,0x01,0x00,0x0c,0x01,0x00,0x22,0x02,0x01,0x00,0x23,0x02,0x00,0x00,0x24,0x02,0x02,0x00,0x4c,0x10};
    feed(&parser, pose_frame, 17u, &battery);
    assert(!parser.pose_updated);
    feed(&parser, pose_frame + 17u, sizeof(pose_frame) - 17u, &battery);
    assert(parser.frame_valid && parser.pose_updated);
    assert(parser.pose[0] == 1 && parser.pose[1] == 0 && parser.pose[2] == 2);
    uint8_t bad_pose[sizeof(pose_frame)];
    memcpy(bad_pose, pose_frame, sizeof(bad_pose)); bad_pose[32] ^= 1u;
    feed(&parser, bad_pose, sizeof(bad_pose), &battery);
    assert(!parser.pose_updated && !parser.frame_valid);
    feed(&parser, pose_frame, sizeof(pose_frame), &battery);
    assert(parser.pose_updated);
    /* RS 4 Pro 04/05 frame captured passively; values are signed tenths. */
    const uint8_t rs4_frame[] = {0x55, 0x3e, 0x04, 0x4b, 0x04, 0x02, 0xae, 0x51, 0x00, 0x04, 0x05, 0xfe, 0x06, 0x00, 0x00, 0x19, 0x00, 0x84, 0x00, 0xea, 0xff, 0x00, 0x01, 0x24, 0x5a, 0x00, 0x00, 0x96, 0xe3, 0x00, 0x00, 0xf3, 0xff, 0xfb, 0xff, 0xa8, 0x79, 0x9f, 0x3b, 0x50, 0xd2, 0x17, 0x3f, 0x9a, 0x1d, 0x4e, 0x3f, 0xf9, 0x39, 0x81, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x45, 0x91};
    feed(&parser, rs4_frame, 29u, &battery);
    assert(!parser.pose_updated);
    feed(&parser, rs4_frame + 29u, sizeof(rs4_frame) - 29u, &battery);
    assert(parser.pose_updated && parser.pose_format == TELEMETRY_POSE_PARAMS49);
    assert(parser.pose[0] == -13 && parser.pose[1] == -5 && parser.pose[2] == 25);
    uint8_t invalid_rs4[sizeof(rs4_frame)];
    memcpy(invalid_rs4, rs4_frame, sizeof(invalid_rs4)); invalid_rs4[11] ^= 1u;
    feed(&parser, invalid_rs4, sizeof(invalid_rs4), &battery);
    assert(!parser.pose_updated && !parser.frame_valid);
    /* A valid-CRC packet outside the observed angle range must stay unavailable. */
    invalid_rs4[31] = 0xffu; invalid_rs4[32] = 0x7fu;
    uint16_t crc = duml_crc16(invalid_rs4, sizeof(invalid_rs4) - 2u);
    invalid_rs4[sizeof(invalid_rs4) - 2u] = (uint8_t)crc;
    invalid_rs4[sizeof(invalid_rs4) - 1u] = (uint8_t)(crc >> 8u);
    feed(&parser, invalid_rs4, sizeof(invalid_rs4), &battery);
    assert(!parser.pose_updated && parser.frame_valid);
    feed(&parser, pose_frame, sizeof(pose_frame), &battery);
    assert(parser.pose_updated && parser.pose_format == TELEMETRY_POSE_TLV);
    const uint8_t rs4_battery[] = {0x55, 0x22, 0x04, 0xea, 0xe5, 0x02, 0x4f, 0x02, 0x00, 0x0d, 0x02, 0x00, 0xb9, 0x3b, 0x00, 0x00, 0x6d, 0xff, 0xff, 0xff, 0xf9, 0x06, 0x00, 0x00, 0x2e, 0x02, 0x00, 0x00, 0x0a, 0x01, 0x04, 0x20, 0x52, 0x19};
    assert(feed(&parser, rs4_battery, sizeof(rs4_battery), &battery) == 1u && battery == 32u);
    memcpy(invalid_rs4, rs4_frame, sizeof(invalid_rs4));
    invalid_rs4[31] = 0xffu; invalid_rs4[32] = 0xffu;
    invalid_rs4[33] = 0x08u; invalid_rs4[34] = 0x07u;
    invalid_rs4[15] = 0xf8u; invalid_rs4[16] = 0xf8u;
    crc = duml_crc16(invalid_rs4, sizeof(invalid_rs4) - 2u);
    invalid_rs4[sizeof(invalid_rs4) - 2u] = (uint8_t)crc;
    invalid_rs4[sizeof(invalid_rs4) - 1u] = (uint8_t)(crc >> 8u);
    feed(&parser, invalid_rs4, sizeof(invalid_rs4), &battery);
    assert(parser.pose_updated && parser.pose[0] == -1 && parser.pose[1] == 1800 && parser.pose[2] == -1800);
    invalid_rs4[8] = 0x80u;
    crc = duml_crc16(invalid_rs4, sizeof(invalid_rs4) - 2u);
    invalid_rs4[sizeof(invalid_rs4) - 2u] = (uint8_t)crc;
    invalid_rs4[sizeof(invalid_rs4) - 1u] = (uint8_t)(crc >> 8u);
    feed(&parser, invalid_rs4, sizeof(invalid_rs4), &battery);
    assert(!parser.pose_updated);
    /* A neighbouring payload length must not be interpreted as this known layout. */
    memcpy(invalid_rs4, rs4_frame, sizeof(invalid_rs4));
    invalid_rs4[1] = sizeof(invalid_rs4) - 1u;
    invalid_rs4[3] = duml_crc8(invalid_rs4, 3u);
    crc = duml_crc16(invalid_rs4, sizeof(invalid_rs4) - 3u);
    invalid_rs4[sizeof(invalid_rs4) - 3u] = (uint8_t)crc;
    invalid_rs4[sizeof(invalid_rs4) - 2u] = (uint8_t)(crc >> 8u);
    feed(&parser, invalid_rs4, sizeof(invalid_rs4) - 1u, &battery);
    assert(parser.frame_valid && !parser.pose_updated);
    /* Isolated RS 4 Pro physical roll, positive and negative, legacy field near zero. */
    const uint8_t roll_positive[] = {0x55, 0x3e, 0x04, 0x4b, 0x04, 0x02, 0x89, 0x2a, 0x00, 0x04, 0x05, 0xf8, 0xf8, 0x00, 0x00, 0xfd, 0xff, 0x84, 0x00, 0x02, 0x00, 0x00, 0x01, 0x14, 0x33, 0x00, 0x00, 0x90, 0xe4, 0x00, 0x00, 0x07, 0x00, 0xa1, 0x01, 0xe6, 0x99, 0x35, 0x3e, 0x3b, 0x07, 0x34, 0xbf, 0x4d, 0x75, 0x24, 0xbf, 0x1a, 0x8e, 0x7d, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0xf3};
    feed(&parser, roll_positive, sizeof(roll_positive), &battery);
    assert(parser.pose_updated && parser.pose[0] == 7 && parser.pose[1] == 417);
    const uint8_t roll_negative[] = {0x55, 0x3e, 0x04, 0x4b, 0x04, 0x02, 0xb1, 0x4d, 0x00, 0x04, 0x05, 0xf8, 0xf8, 0x00, 0x00, 0xf9, 0xff, 0x84, 0x00, 0x09, 0x00, 0x00, 0x01, 0x3c, 0x56, 0x00, 0x00, 0x90, 0xe4, 0x00, 0x00, 0x02, 0x00, 0xe1, 0xfe, 0xe0, 0x47, 0xe8, 0x3d, 0xaf, 0xa8, 0xe2, 0x3e, 0x37, 0xb3, 0x5f, 0x3f, 0x46, 0x11, 0x2a, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x5a};
    feed(&parser, roll_negative, sizeof(roll_negative), &battery);
    assert(parser.pose_updated && parser.pose[0] == 2 && parser.pose[1] == -287);
    /* Calibrated Focus Pro Motor status captured while its gear drove the zoom ring. */
    const uint8_t focus_frame[] = {0x55, 0x1c, 0x04, 0x1b, 0xbf, 0x02, 0x5f, 0xa0, 0x00, 0x04, 0x2f, 0x09, 0x00, 0x03, 0xa3, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x5b};
    feed(&parser, focus_frame, sizeof(focus_frame), &battery);
    assert(parser.focus_updated && parser.focus_calibrated && parser.focus_position == 1187u && parser.focus_address == 0xbfu);
    /* RS 3 Focus Pro Motor position frame captured on the connected gimbal. */
    uint8_t rs3_focus_frame[sizeof(focus_frame)];
    memcpy(rs3_focus_frame, focus_frame, sizeof(focus_frame));
    rs3_focus_frame[4] = 0xdfu;
    rs3_focus_frame[14] = 0x49u;
    rs3_focus_frame[15] = 0x0fu;
    crc = duml_crc16(rs3_focus_frame, sizeof(rs3_focus_frame) - 2u);
    rs3_focus_frame[sizeof(rs3_focus_frame) - 2u] = (uint8_t)crc;
    rs3_focus_frame[sizeof(rs3_focus_frame) - 1u] = (uint8_t)(crc >> 8u);
    feed(&parser, rs3_focus_frame, sizeof(rs3_focus_frame), &battery);
    assert(parser.focus_updated && parser.focus_calibrated && parser.focus_position == 3913u && parser.focus_address == 0xdfu);
    uint8_t focus_bad[sizeof(focus_frame)];
    memcpy(focus_bad, focus_frame, sizeof(focus_frame)); focus_bad[14] ^= 1u;
    feed(&parser, focus_bad, sizeof(focus_bad), &battery);
    assert(!parser.focus_updated && !parser.frame_valid);
    for(unsigned case_id = 0u; case_id < 5u; case_id++)
    {
        memcpy(focus_bad, focus_frame, sizeof(focus_frame));
        if(case_id == 0u) { focus_bad[14] = 0xffu; focus_bad[15] = 0x0fu; }
        if(case_id == 1u) { focus_bad[14] = 0u; focus_bad[15] = 0x10u; }
        if(case_id == 2u) { focus_bad[13] = 0u; }
        if(case_id == 3u) { focus_bad[4] = 0xe5u; }
        if(case_id == 4u) { focus_bad[8] = 0x80u; }
        crc = duml_crc16(focus_bad, sizeof(focus_bad) - 2u);
        focus_bad[sizeof(focus_bad) - 2u] = (uint8_t)crc;
        focus_bad[sizeof(focus_bad) - 1u] = (uint8_t)(crc >> 8u);
        feed(&parser, focus_bad, sizeof(focus_bad), &battery);
        if(case_id == 0u) { assert(parser.focus_updated && parser.focus_calibrated && parser.focus_position == 4095u); }
        if((case_id == 1u) || (case_id == 2u)) { assert(parser.focus_updated && !parser.focus_calibrated); }
        if(case_id >= 3u) { assert(!parser.focus_updated); }
    }
    puts("Telemetry tests passed");
}
