#include "telemetry.h"
#include "duml.h"

bool telemetry_feed(telemetry_parser_t *parser, uint8_t byte, uint8_t *battery)
{
    if((parser == NULL) || (battery == NULL)) { return false; }
    parser->pose_updated = false;
    parser->focus_updated = false;
    parser->frame_valid = false;
    parser->sleep_updated = false;
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
    parser->frame_valid = duml_crc16(parser->frame, length - 2u) == crc;
    bool valid = parser->frame_valid &&
                 ((parser->frame[8] & 0x80u) == 0u) &&
                 (parser->frame[9] == 0x0du) && (parser->frame[10] == 0x02u) &&
                 (length == 34u) && (parser->frame[31] <= 100u);
    if(valid) { *battery = parser->frame[31]; }
    /* RS 3 sleep status, correlated with a physical sleep/wake cycle. */
    if(parser->frame_valid && (length == 18u) && (parser->frame[4] == 0x04u) &&
       (parser->frame[5] == 0x02u) && (parser->frame[8] == 0u) &&
       (parser->frame[9] == 0x04u) && (parser->frame[10] == 0x27u) &&
       (parser->frame[11] == 0u) && (parser->frame[12] == 0u) &&
       (parser->frame[13] == 0u) && (parser->frame[14] == 0u) &&
       (parser->frame[15] <= 1u))
    {
        parser->sleeping = parser->frame[15] == 1u;
        parser->sleep_updated = true;
    }
    if(parser->frame_valid &&
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
        { for(unsigned i = 0u; i < 3u; i++) { parser->pose[i] = pose[i]; } parser->pose_format = TELEMETRY_POSE_TLV; parser->pose_updated = true; }
    }
    /* The RS 4 Pro capture has a 49-byte Gimbal Params payload (04/05).
       Accept only the observed direction, length and bounded signed angles. */
    if(parser->frame_valid && (parser->frame[4] == 0x04u) && (parser->frame[5] == 0x02u) &&
       (parser->frame[8] == 0u) && (parser->frame[9] == 0x04u) &&
       (parser->frame[10] == 0x05u) && (length == 62u))
    {
        /* Read tilt and roll from the same attitude pair. The legacy tilt
           at offset 0 reports about +/-180 degrees for a level camera.
           Offset 22 followed the isolated physical roll (+41.7 / -28.7 deg). */
        static const uint8_t offsets[3] = {20u, 22u, 4u};
        int16_t pose[3] = {0};
        bool bounded = true;
        for(unsigned i = 0u; i < 3u; i++)
        {
            size_t offset = 11u + offsets[i];
            uint16_t raw = (uint16_t)parser->frame[offset] | ((uint16_t)parser->frame[offset + 1u] << 8u);
            pose[i] = (int16_t)raw;
            if((pose[i] < -1800) || (pose[i] > 1800)) { bounded = false; }
        }
        if(bounded)
        {
            for(unsigned i = 0u; i < 3u; i++) { parser->pose[i] = pose[i]; }
            parser->pose_format = TELEMETRY_POSE_PARAMS49;
            parser->pose_updated = true;
        }
    }
    /* Captured Focus Pro Motor push: source BF (RS 4 Pro) or DF (RS 3),
       status subcommand 09, motor type 00, calibration, then position. */
    if(parser->frame_valid && ((parser->frame[4] == 0xbfu) || (parser->frame[4] == 0xdfu)) &&
       (parser->frame[5] == 0x02u) &&
       (parser->frame[8] == 0u) && (parser->frame[9] == 0x04u) &&
       (parser->frame[10] == 0x2fu) && (length == 28u) &&
       (parser->frame[11] == 0x09u) && (parser->frame[12] == 0u) && (parser->frame[13] <= 3u))
    {
        uint32_t position = (uint32_t)parser->frame[14] | ((uint32_t)parser->frame[15] << 8u) |
                            ((uint32_t)parser->frame[16] << 16u) | ((uint32_t)parser->frame[17] << 24u);
        parser->focus_calibrated = (parser->frame[13] == 3u) && (position <= 4095u);
        parser->focus_position = parser->focus_calibrated ? (uint16_t)position : 0u;
        parser->focus_address = parser->frame[4];
        parser->focus_updated = true;
    }
    parser->used = 0u; parser->expected = 0u;
    return valid;
}
