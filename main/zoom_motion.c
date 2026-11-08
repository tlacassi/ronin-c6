#include "zoom_motion.h"
#include <stddef.h>

bool zoom_motion_start(zoom_motion_t *motion, uint16_t position, uint16_t target, uint8_t speed, uint32_t now_ms)
{
    if((motion == NULL) || (position > ZOOM_POSITION_MAX) || (target > ZOOM_POSITION_MAX) ||
       (speed < 1u) || (speed > 100u))
    {
        return false;
    }
    *motion = (zoom_motion_t){.position = position, .target = target, .speed = speed,
                              .last_tick_ms = now_ms, .lease_ms = now_ms, .hold_ms = now_ms,
                              .active = true, .holding = position == target};
    return true;
}
void zoom_motion_renew(zoom_motion_t *motion, uint32_t now_ms)
{
    if((motion != NULL) && motion->active)
    {
        motion->lease_ms = now_ms;
    }
}
void zoom_motion_cancel(zoom_motion_t *motion, zoom_error_t error)
{
    if(motion != NULL)
    {
        motion->active = false;
        motion->error = error;
    }
}
bool zoom_motion_tick(zoom_motion_t *motion, uint32_t now_ms, bool available, uint16_t *position)
{
    if((motion == NULL) || (position == NULL) || !motion->active)
    {
        return false;
    }
    uint32_t elapsed = now_ms - motion->last_tick_ms;
    if(!available)
    {
        zoom_motion_cancel(motion, ZOOM_ERROR_UNAVAILABLE);
        return false;
    }
    if((uint32_t)(now_ms - motion->lease_ms) > ZOOM_LEASE_MS)
    {
        zoom_motion_cancel(motion, ZOOM_ERROR_LEASE);
        return false;
    }
    if(elapsed > 100u)
    {
        zoom_motion_cancel(motion, ZOOM_ERROR_SCHEDULING);
        return false;
    }
    motion->last_tick_ms = now_ms;
    if(motion->holding)
    {
        /* Keep the final target alive long enough for the observed motor stream. */
        if((uint32_t)(now_ms - motion->hold_ms) >= 300u)
        {
            zoom_motion_cancel(motion, ZOOM_ERROR_NONE);
            return false;
        }
    }
    else
    {
        uint32_t numerator = motion->remainder + ZOOM_POSITION_MAX * (uint32_t)motion->speed * elapsed;
        uint32_t step = numerator / 100000u;
        motion->remainder = numerator % 100000u;
        uint32_t distance = motion->target > motion->position ?
                            (uint32_t)motion->target - motion->position : (uint32_t)motion->position - motion->target;
        if(step >= distance)
        {
            motion->position = motion->target;
            motion->holding = true;
            motion->hold_ms = now_ms;
        }
        else if(motion->target > motion->position)
        {
            motion->position = (uint16_t)(motion->position + step);
        }
        else
        {
            motion->position = (uint16_t)(motion->position - step);
        }
    }
    *position = motion->position;
    return true;
}
