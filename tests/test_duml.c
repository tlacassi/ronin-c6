#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "duml.h"
int main(void)
{
    const uint8_t captured[] = {0x55,0x1a,0x04,0xb1,0x04,0x02,0x00,0x00,0x00,0x1c,0x01,0x03,0x01,0x00,0x01,0x00,0x02,0x00,0x01,0xff,0x03,0x00,0x01,0x00,0x5b,0x10};
    assert(duml_crc8(captured, 3) == captured[3]);
    assert(duml_crc16(captured, sizeof(captured)-2) == 0x105b);
    uint8_t sleep_frame[16] = {0};
    duml_sleep(sleep_frame, 0x1234u, true);
    assert(sleep_frame[4] == 2u && sleep_frame[5] == 4u && sleep_frame[8] == 0x40u);
    assert(sleep_frame[9] == 4u && sleep_frame[10] == 0x0fu && sleep_frame[11] == 0x23u && sleep_frame[12] == 1u && sleep_frame[13] == 1u);
    assert(duml_crc16(sleep_frame, 14u) == (uint16_t)(sleep_frame[14] | (sleep_frame[15] << 8u)));
    duml_sleep(sleep_frame, 0u, false);
    assert(sleep_frame[13] == 0u && duml_crc8(sleep_frame, 3u) == sleep_frame[3]);
    assert(duml_crc16(sleep_frame, 14u) == (uint16_t)(sleep_frame[14] | (sleep_frame[15] << 8u)));
    uint8_t frame[20] = {0};
    duml_speed(frame, 0x1234, -300, 0, 300);
    assert(frame[6] == 0x34 && frame[7] == 0x12);
    assert(frame[11] == 0xd4 && frame[12] == 0xfe);
    assert(frame[15] == 0x2c && frame[16] == 0x01 && frame[17] == 0x80);
    assert(duml_crc16(frame, 18) == (uint16_t)(frame[18] | (frame[19] << 8)));
    uint8_t poll[DUML_STATUS_POLL_MAX] = {0};
    const size_t expected_lengths[] = {36u, 27u, 33u};
    for(unsigned stage = 0u; stage < 3u; stage++)
    {
        size_t length = duml_status_poll(poll, 0x1234u, stage);
        assert(length == expected_lengths[stage]);
        assert(poll[0] == 0x55u && poll[1] == length && poll[3] == duml_crc8(poll, 3u));
        assert(poll[4] == 0x02u && poll[5] == 0xe5u && poll[8] == 0u);
        assert(poll[9] == 0x04u && poll[10] == 0x12u && poll[11] == 0x66u);
        assert(duml_crc16(poll, length - 2u) == (uint16_t)(poll[length - 2u] | (poll[length - 1u] << 8u)));
    }
    assert(poll[12] == 0x24u && poll[30] == 0x03u);
    assert(duml_status_poll(poll, 0u, 3u) == 0u);
    assert(duml_status_poll(NULL, 0u, 0u) == 0u);
    uint8_t angle[21] = {0};
    duml_angle(angle, 0x1234u, 123, -30, -1500, 0x0bu, 20u);
    assert(angle[11] == 123u && angle[12] == 0u);
    assert(angle[13] == 0xe2u && angle[14] == 0xffu);
    assert(angle[15] == 0x24u && angle[16] == 0xfau);
    assert(angle[17] == 0x0bu && angle[18] == 20u);
    assert(duml_crc8(angle, 3u) == angle[3]);
    assert(duml_crc16(angle, 19u) == (uint16_t)(angle[19] | (angle[20] << 8u)));
    duml_angle(angle, 1u, -15, -5, 60, 0x01u, 20u);
    assert(angle[11] == 0xf1u && angle[12] == 0xffu);
    assert(angle[13] == 0xfbu && angle[14] == 0xffu);
    assert(angle[15] == 60u && angle[16] == 0u && angle[17] == 0x01u);
    assert(duml_crc16(angle, 19u) == (uint16_t)(angle[19] | (angle[20] << 8u)));
    uint8_t focus[18] = {0};
    assert(duml_focus(focus, 0x1234u, 4074u));
    assert(focus[1] == sizeof(focus) && focus[4] == 0x02u && focus[5] == 0xbfu);
    assert(focus[9] == 0x04u && focus[10] == 0x2fu && focus[11] == 1u && focus[12] == 0u && focus[13] == 2u);
    assert(focus[14] == 0xeau && focus[15] == 0x0fu);
    assert(duml_crc8(focus, 3u) == focus[3]);
    assert(duml_crc16(focus, 16u) == (uint16_t)(focus[16] | (focus[17] << 8u)));
    assert(!duml_focus(focus, 0u, 4096u) && !duml_focus(NULL, 0u, 0u));
    assert(duml_focus_motor(focus, 0x1235u, 0xdfu, 3913u));
    assert(focus[4] == 0x02u && focus[5] == 0xdfu && focus[14] == 0x49u && focus[15] == 0x0fu);
    assert(duml_crc16(focus, 16u) == (uint16_t)(focus[16] | (focus[17] << 8u)));
    assert(!duml_focus_motor(focus, 0u, 0xe5u, 0u));
    puts("DUML tests passed");
}
