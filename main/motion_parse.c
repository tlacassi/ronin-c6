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
        if(magnitude > 300u)
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
