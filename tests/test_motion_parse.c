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
    puts("Motion parser tests passed");
}
