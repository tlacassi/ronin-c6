#include "preset_data.h"
#include <string.h>
_Static_assert(sizeof(preset_axes_t) == 44u, "Legacy NVS layout must remain stable");
_Static_assert(sizeof(preset_t) == 48u, "Extended NVS layout must remain stable");
bool preset_decode(const void *data, size_t length, uint32_t magic, preset_t *preset)
{
    if((data == NULL) || (preset == NULL) || ((length != sizeof(preset_axes_t)) && (length != sizeof(preset_t))))
    {
        return false;
    }
    preset_t decoded = {0};
    memcpy(&decoded, data, length);
    if((decoded.axes.magic != magic) || (decoded.axes.name[PRESET_NAME_MAX] != '\0') ||
       (decoded.zoom_valid > 1u) ||
       (decoded.zoom_valid && ((decoded.zoom_position > 4095u) || (decoded.zoom_motor != 0xbfu))))
    {
        return false;
    }
    for(unsigned i = 0u; i < 3u; i++)
    {
        if((decoded.axes.pose[i] < -1800) || (decoded.axes.pose[i] > 1800))
        {
            return false;
        }
    }
    *preset = decoded;
    return true;
}
