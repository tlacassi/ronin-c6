#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "motion_parse.h"
int main(void)
{
    const char *bad[] = {"", ",", "1,", ",2", "+1,2", " 1,2", "1,2x", "601,0", "-601,0", "0,99999999999999999999999", "1,2,3", "--1,0"};
    int16_t yaw = 7, pitch = 8;
    assert(motion_parse("-600,600", 8, &yaw, &pitch));
    assert(yaw == -600 && pitch == 600);
    assert(motion_parse("0,0", 3, &yaw, &pitch));
    assert(yaw == 0 && pitch == 0);
    for(size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
    {
        yaw = 7; pitch = 8;
        assert(!motion_parse(bad[i], strlen(bad[i]), &yaw, &pitch));
        assert(yaw == 7 && pitch == 8);
    }
    const char embedded[] = {'1', ',', '2', '\0', '3'};
    assert(!motion_parse(embedded, sizeof(embedded), &yaw, &pitch));
    uint16_t zoom = 7u; uint8_t speed = 8u;
    assert(zoom_parse("4095,100", 8u, &zoom, &speed) && zoom == 4095u && speed == 100u);
    assert(zoom_parse("0,1", 3u, &zoom, &speed) && zoom == 0u && speed == 1u);
    const char *zoom_bad[] = {"", "4096,20", "1,0", "1,101", "-1,20", "1,-1", ",20", "1,", "1,2,3", "123456789,1", "0, 1"};
    for(size_t i = 0u; i < sizeof(zoom_bad) / sizeof(zoom_bad[0]); i++)
    {
        zoom = 7u; speed = 8u;
        assert(!zoom_parse(zoom_bad[i], strlen(zoom_bad[i]), &zoom, &speed));
        assert(zoom == 7u && speed == 8u);
    }
    puts("Motion parser tests passed");
}
