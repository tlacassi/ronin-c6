#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "duml.h"
int main(void)
{
    const uint8_t captured[] = {0x55,0x1a,0x04,0xb1,0x04,0x02,0x00,0x00,0x00,0x1c,0x01,0x03,0x01,0x00,0x01,0x00,0x02,0x00,0x01,0xff,0x03,0x00,0x01,0x00,0x5b,0x10};
    assert(duml_crc8(captured, 3) == captured[3]);
    assert(duml_crc16(captured, sizeof(captured)-2) == 0x105b);
    uint8_t frame[20] = {0};
    duml_speed(frame, 0x1234, -300, 0, 300);
    assert(frame[6] == 0x34 && frame[7] == 0x12);
    assert(frame[11] == 0xd4 && frame[12] == 0xfe);
    assert(frame[15] == 0x2c && frame[16] == 0x01 && frame[17] == 0x80);
    assert(duml_crc16(frame, 18) == (uint16_t)(frame[18] | (frame[19] << 8)));
    puts("DUML tests passed");
}
