#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "zoom_motion.h"
int main(void)
{
    zoom_motion_t motion = {0}; uint16_t position = 0u;
    assert(!zoom_motion_start(&motion, 0u, 4096u, 20u, 0u));
    assert(!zoom_motion_start(&motion, 0u, 4095u, 0u, 0u));
    assert(!zoom_motion_start(&motion, 0u, 4095u, 101u, 0u));
    assert(zoom_motion_start(&motion, 0u, 4095u, 100u, 0u));
    for(uint32_t t = 10u; t <= 1000u; t += 10u)
    {
        assert(zoom_motion_tick(&motion, t, true, &position));
        assert(position == (uint16_t)(4095u * t / 1000u));
    }
    assert(position == 4095u && motion.active && motion.holding);
    for(uint32_t t = 1010u; t < 1300u; t += 10u) { assert(zoom_motion_tick(&motion, t, true, &position)); }
    assert(!zoom_motion_tick(&motion, 1300u, true, &position) && !motion.active);
    assert(zoom_motion_start(&motion, 4095u, 0u, 1u, 0u));
    for(uint32_t t = 10u; t <= 1000u; t += 10u) { assert(zoom_motion_tick(&motion, t, true, &position)); }
    assert(position == 4055u);
    zoom_motion_renew(&motion, 1000u);
    for(uint32_t t = 1010u; t <= 2000u; t += 10u) { assert(zoom_motion_tick(&motion, t, true, &position)); }
    assert(!zoom_motion_tick(&motion, 2001u, false, &position));
    assert(motion.error == ZOOM_ERROR_UNAVAILABLE);
    assert(!zoom_motion_tick(&motion, 2010u, true, &position));
    assert(zoom_motion_start(&motion, 0u, 4095u, 1u, 0u));
    for(uint32_t t = 10u; t <= 1500u; t += 10u) { assert(zoom_motion_tick(&motion, t, true, &position)); }
    assert(!zoom_motion_tick(&motion, 1510u, true, &position) && motion.error == ZOOM_ERROR_LEASE);
    assert(zoom_motion_start(&motion, 0u, 4095u, 20u, 0u));
    assert(!zoom_motion_tick(&motion, 101u, true, &position) && motion.error == ZOOM_ERROR_SCHEDULING);
    uint32_t start = UINT32_MAX - 50u;
    assert(zoom_motion_start(&motion, 4000u, 3900u, 20u, start));
    for(uint32_t elapsed = 10u; elapsed <= 500u; elapsed += 10u)
    {
        zoom_motion_renew(&motion, start + elapsed);
        (void)zoom_motion_tick(&motion, start + elapsed, true, &position);
    }
    assert(position == 3900u && !motion.active && motion.error == ZOOM_ERROR_NONE);
    puts("Zoom trajectory tests passed");
}
