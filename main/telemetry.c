#include "telemetry.h"
#include "duml.h"

bool telemetry_feed(telemetry_parser_t *parser, uint8_t byte, uint8_t *battery)
{
    if((parser == NULL) || (battery == NULL)) { return false; }
    parser->pose_updated = false;
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
    if((duml_crc16(parser->frame, length - 2u) == crc) &&
       ((parser->frame[8] & 0x80u) == 0u) && (parser->frame[9] == 0x04u) &&
       (parser->frame[10] == 0x66u) && (length >= 14u))
    {
        size_t offset = 12u;
        uint8_t mask = 0u;
        int16_t pose[3] = {0};
        while(offset + 2u <= length - 2u)
        {
            uint8_t tag = parser->frame[offset++];
            uint8_t n = parser->frame[offset++];
            if(offset + n > length - 2u) { mask = 0u; break; }
            if((tag >= 0x22u) && (tag <= 0x24u) && (n == 2u))
            {
                unsigned i = tag - 0x22u;
                uint16_t raw = (uint16_t)parser->frame[offset] | ((uint16_t)parser->frame[offset + 1u] << 8u);
                pose[i] = (int16_t)raw;
                mask |= (uint8_t)(1u << i);
            }
            offset += n;
        }
        if((mask == 7u) && (offset == length - 2u))
        { for(unsigned i = 0u; i < 3u; i++) { parser->pose[i] = pose[i]; } parser->pose_updated = true; }
    }
    parser->used = 0u; parser->expected = 0u;
    return valid;
}
