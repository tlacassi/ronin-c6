#include "telemetry.h"
#include "duml.h"

bool telemetry_feed(telemetry_parser_t *parser, uint8_t byte, uint8_t *battery)
{
    if((parser == NULL) || (battery == NULL)) { return false; }
    if((parser->used == 0u) && (byte != 0x55u)) { return false; }
    parser->frame[parser->used++] = byte;
    if(parser->used == 4u)
    {
        size_t length = (size_t)parser->frame[1] | (((size_t)parser->frame[2] & 3u) << 8u);
        if((duml_crc8(parser->frame, 3u) != parser->frame[3]) ||
           (length < 13u) || (length > TELEMETRY_FRAME_MAX))
        { parser->used = 0u; parser->expected = 0u; return false; }
        parser->expected = length;
    }
    if((parser->expected == 0u) || (parser->used < parser->expected)) { return false; }
    size_t length = parser->expected;
    uint16_t crc = (uint16_t)parser->frame[length - 2u] | ((uint16_t)parser->frame[length - 1u] << 8u);
    bool valid = (duml_crc16(parser->frame, length - 2u) == crc) &&
                 ((parser->frame[8] & 0x80u) == 0u) &&
                 (parser->frame[9] == 0x0du) && (parser->frame[10] == 0x02u) &&
                 (length == 34u) && (parser->frame[31] <= 100u);
    if(valid) { *battery = parser->frame[31]; }
    parser->used = 0u; parser->expected = 0u;
    return valid;
}
