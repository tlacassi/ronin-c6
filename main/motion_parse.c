#include "motion_parse.h"

static bool parse_axis(const char *data, size_t len, size_t *pos, int16_t *value)
{
    bool negative = false;
    unsigned int magnitude = 0u;
    size_t digits = 0u;
    if((*pos < len) && (data[*pos] == '-'))
    {
        negative = true;
        (*pos)++;
    }
    while((*pos < len) && (data[*pos] >= '0') && (data[*pos] <= '9'))
    {
        magnitude = magnitude * 10u + (unsigned int)(data[*pos] - '0');
        if(magnitude > 600u)
        {
            return false;
        }
        (*pos)++;
        digits++;
    }
    if(digits == 0u)
    {
        return false;
    }
    *value = negative ? (int16_t)-(int)magnitude : (int16_t)magnitude;
    return true;
}
bool motion_parse(const char *data, size_t len, int16_t *yaw, int16_t *pitch)
{
    size_t pos = 0u;
    int16_t parsed_yaw = 0;
    int16_t parsed_pitch = 0;
    if((data == NULL) || (yaw == NULL) || (pitch == NULL) ||
       !parse_axis(data, len, &pos, &parsed_yaw) || (pos >= len) || (data[pos] != ','))
    {
        return false;
    }
    pos++;
    if(!parse_axis(data, len, &pos, &parsed_pitch) || (pos != len))
    {
        return false;
    }
    *yaw = parsed_yaw;
    *pitch = parsed_pitch;
    return true;
}

bool zoom_parse(const char *data, size_t len, uint16_t *position, uint8_t *speed)
{
    if((data == NULL) || (position == NULL) || (speed == NULL) || (len == 0u) || (len > 8u))
    {
        return false;
    }
    unsigned values[2] = {0u, 0u};
    unsigned field = 0u;
    unsigned digits = 0u;
    for(size_t i = 0u; i < len; i++)
    {
        if((data[i] == ',') && (field == 0u) && (digits > 0u))
        {
            field = 1u;
            digits = 0u;
        }
        else if((data[i] >= '0') && (data[i] <= '9'))
        {
            values[field] = values[field] * 10u + (unsigned)(data[i] - '0');
            if(values[field] > (field == 0u ? 4095u : 100u)) { return false; }
            digits++;
        }
        else
        {
            return false;
        }
    }
    if((field != 1u) || (digits == 0u) || (values[1] == 0u)) { return false; }
    *position = (uint16_t)values[0];
    *speed = (uint8_t)values[1];
    return true;
}
