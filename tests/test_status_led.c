#include <assert.h>
#include <stdio.h>
#include "status_led_pattern.h"
int main(void)
{
    uint8_t c[3];
    status_led_pattern(STATUS_LED_NO_WIFI, 100u, c);  assert(c[0] == 255 && c[1] == 0 && c[2] == 0);
    status_led_pattern(STATUS_LED_NO_WIFI, 700u, c);  assert(c[0] == 0 && c[1] == 0 && c[2] == 0);
    status_led_pattern(STATUS_LED_NO_RONIN, 100u, c); assert(c[0] == 0 && c[1] == 0 && c[2] == 255);
    status_led_pattern(STATUS_LED_NO_RONIN, 1700u, c);assert(c[2] == 0);
    status_led_pattern(STATUS_LED_BOOT, 50u, c);      assert(c[0] == 255 && c[1] == 255 && c[2] == 255);
    status_led_pattern(STATUS_LED_BOOT, 150u, c);     assert(c[0] == 0);
    status_led_pattern(STATUS_LED_BOOT, 250u, c);     assert(c[0] == 255);
    status_led_pattern(STATUS_LED_BOOT, 600u, c);     assert(c[0] == 0);
    status_led_pattern(STATUS_LED_OK, 0u, c);         assert(c[0] == 0 && c[1] == 0 && c[2] == 0);
    status_led_pattern(STATUS_LED_OK, 2000u, c);      assert(c[1] >= 254 && c[0] == 0 && c[2] == 0);
    status_led_pattern(STATUS_LED_OK, 6000u, c);      assert(c[1] >= 254);
    uint8_t prev = 0;
    for(uint32_t t = 0; t <= 2000; t += 50) { status_led_pattern(STATUS_LED_OK, t, c); assert(c[1] >= prev); prev = c[1]; }
    puts("status led pattern tests passed");
    return 0;
}
