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
void duml_sleep(uint8_t out[16], uint16_t seq, bool sleep)
{
    out[0] = 0x55u; out[1] = 16u; out[2] = 0x04u;
    out[3] = duml_crc8(out, 3u);
    out[4] = 0x02u; out[5] = 0x04u; put16(&out[6], seq);
    out[8] = 0x40u; out[9] = 0x04u; out[10] = 0x0fu;
    out[11] = 0x23u; out[12] = 0x01u; out[13] = sleep ? 1u : 0u;
    put16(&out[14], duml_crc16(out, 14u));
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

/* App status polling captured by jdesbonnet/dji_rs3_control; see NOTICE. */
size_t duml_status_poll(uint8_t out[DUML_STATUS_POLL_MAX], uint16_t seq, unsigned stage)
{
    static const uint8_t payload0[] = {0x66u, 0x0cu, 0xc0u, 0x1du, 0x10u, 0x84u, 0x01u, 0x00u, 0x0eu, 0x00u, 0x0cu, 0x00u, 0x00u, 0x50u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x10u};
    static const uint8_t payload1[] = {0x66u, 0x0cu, 0xc0u, 0x1du, 0x10u, 0x3eu, 0x01u, 0x00u, 0x00u, 0x00u, 0x0cu, 0x00u, 0x00u, 0x50u};
    static const uint8_t payload2[] = {0x66u, 0x24u, 0xc0u, 0x1du, 0x00u, 0x00u, 0x1cu, 0x10u, 0x51u, 0x01u, 0x00u, 0x00u, 0x00u, 0x0cu, 0x00u, 0x00u, 0x50u, 0x00u, 0xf1u, 0x03u};
    const uint8_t *payloads[] = {payload0, payload1, payload2};
    const size_t lengths[] = {sizeof(payload0), sizeof(payload1), sizeof(payload2)};
    if((out == NULL) || (stage >= 3u)) { return 0u; }
    size_t length = 13u + lengths[stage];
    out[0] = 0x55u; out[1] = (uint8_t)length; out[2] = 0x04u;
    out[3] = duml_crc8(out, 3u);
    out[4] = 0x02u; out[5] = 0xe5u; put16(&out[6], seq);
    out[8] = 0u; out[9] = 0x04u; out[10] = 0x12u;
    for(size_t i = 0u; i < lengths[stage]; i++) { out[11u + i] = payloads[stage][i]; }
    put16(&out[length - 2u], duml_crc16(out, length - 2u));
    return length;
}

void duml_angle(uint8_t out[21], uint16_t seq, int16_t yaw, int16_t roll, int16_t pitch, uint8_t flags, uint8_t duration)
{
    out[0] = 0x55u; out[1] = 21u; out[2] = 0x04u;
    out[3] = duml_crc8(out, 3u);
    out[4] = 0x02u; out[5] = 0x04u; put16(&out[6], seq);
    out[8] = 0x40u; out[9] = 0x04u; out[10] = 0x14u;
    put16(&out[11], (uint16_t)yaw); put16(&out[13], (uint16_t)roll);
    put16(&out[15], (uint16_t)pitch);
    out[17] = flags; out[18] = duration;
    put16(&out[19], duml_crc16(out, 19u));
}

bool duml_focus_motor(uint8_t out[18], uint16_t seq, uint8_t address, uint16_t position)
{
    if((out == NULL) || (position > 4095u) || ((address != 0xbfu) && (address != 0xdfu))) { return false; }
    out[0] = 0x55u; out[1] = 18u; out[2] = 0x04u;
    out[3] = duml_crc8(out, 3u);
    out[4] = 0x02u; out[5] = address; put16(&out[6], seq);
    out[8] = 0x40u; out[9] = 0x04u; out[10] = 0x2fu;
    out[11] = 0x01u; out[12] = 0x00u; out[13] = 0x02u;
    put16(&out[14], position); put16(&out[16], duml_crc16(out, 16u));
    return true;
}
bool duml_focus(uint8_t out[18], uint16_t seq, uint16_t position)
{
    return duml_focus_motor(out, seq, 0xbfu, position);
}
