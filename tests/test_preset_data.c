#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "preset_data.h"
int main(void)
{
    const uint32_t magic = 0x52535033u;
    preset_axes_t legacy = {.magic = magic, .pose = {-37, -5, 18}, .name = "DJI RS4 PRO-090BGG"};
    preset_t decoded = {0};
    assert(preset_decode(&legacy, sizeof(legacy), magic, &decoded));
    assert(memcmp(&decoded.axes, &legacy, sizeof(legacy)) == 0 && !decoded.zoom_valid);
    preset_t extended = {.axes = legacy, .zoom_position = 4074u, .zoom_valid = 1u, .zoom_motor = 0xbfu};
    assert(preset_decode(&extended, sizeof(extended), magic, &decoded));
    assert(decoded.zoom_valid && decoded.zoom_position == 4074u);
    extended.zoom_position = 4096u;
    assert(!preset_decode(&extended, sizeof(extended), magic, &decoded));
    extended.zoom_valid = 0u;
    assert(preset_decode(&extended, sizeof(extended), magic, &decoded));
    assert(!preset_decode(&extended, sizeof(extended) - 1u, magic, &decoded));
    assert(!preset_decode(&legacy, sizeof(legacy), 0x52535032u, &decoded));
    legacy.name[PRESET_NAME_MAX] = 'X';
    assert(!preset_decode(&legacy, sizeof(legacy), magic, &decoded));
    puts("Preset compatibility tests passed");
}
