#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "telemetry.h"
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
    assert(feed(&parser, frame, 12u, &battery) == 0u);
    assert(feed(&parser, frame + 12u, sizeof(frame) - 12u, &battery) == 1u);
    assert(battery == 97u);
    uint8_t damaged[sizeof(frame)]; memcpy(damaged, frame, sizeof(frame)); damaged[31] ^= 1u;
    assert(feed(&parser, damaged, sizeof(damaged), &battery) == 0u);
    assert(feed(&parser, frame, sizeof(frame), &battery) == 1u);
    assert(feed(&parser, frame, sizeof(frame), &battery) == 1u);
    puts("Telemetry tests passed");
}
