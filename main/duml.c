/* DUML framing derived from DJI-Ronin-Bluetooth-Connector, MIT license; see NOTICE. */
#include "duml.h"
uint8_t duml_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x77u;
    for(size_t i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for(unsigned bit = 0u; bit < 8u; bit++)
        {
            crc = (crc & 1u) ? (uint8_t)((crc >> 1u) ^ 0x8cu) : (uint8_t)(crc >> 1u);
        }
    }
    return crc;
}
uint16_t duml_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x3692u;
    for(size_t i = 0u; i < len; i++)
    {
        crc ^= data[i];
        for(unsigned bit = 0u; bit < 8u; bit++)
        {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1u) ^ 0x8408u) : (uint16_t)(crc >> 1u);
        }
    }
    return crc;
}
static void put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
}
void duml_speed(uint8_t out[20], uint16_t seq, int16_t yaw, int16_t roll, int16_t pitch)
{
    out[0] = 0x55u; out[1] = 20u; out[2] = 0x04u;
    out[3] = duml_crc8(out, 3u);
    out[4] = 0x02u; out[5] = 0x04u; put16(&out[6], seq);
    out[8] = 0u; out[9] = 0x04u; out[10] = 0x0cu;
    put16(&out[11], (uint16_t)yaw); put16(&out[13], (uint16_t)roll);
    put16(&out[15], (uint16_t)pitch); out[17] = 0x80u;
    put16(&out[18], duml_crc16(out, 18u));
}
