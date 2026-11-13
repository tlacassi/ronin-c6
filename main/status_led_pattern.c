#include "status_led_pattern.h"
static uint8_t blink(uint32_t phase_ms)
{
    return (phase_ms < 500u) ? 255u : 0u;
}
static uint8_t breathe(uint32_t phase_ms)
{
    uint32_t l_u32Tri = (phase_ms < 2000u) ? phase_ms : (4000u - phase_ms);
    return (uint8_t)((l_u32Tri * l_u32Tri) / 15686u); /* 2000^2 / 255 */
}
void status_led_pattern(status_led_state_t state, uint32_t time_ms, uint8_t rgb[3])
{
    rgb[0] = 0u; rgb[1] = 0u; rgb[2] = 0u;
    switch(state)
    {
    case STATUS_LED_BOOT:
    {
        uint32_t l_u32Phase = time_ms % 1200u;
        uint8_t l_u8On = ((l_u32Phase < 100u) || ((l_u32Phase >= 200u) && (l_u32Phase < 300u))) ? 255u : 0u;
        rgb[0] = l_u8On; rgb[1] = l_u8On; rgb[2] = l_u8On;
        break;
    }
    case STATUS_LED_NO_WIFI: rgb[0] = blink(time_ms % 1000u); break;
    case STATUS_LED_NO_RONIN: rgb[2] = blink(time_ms % 1000u); break;
    case STATUS_LED_OK: rgb[1] = breathe(time_ms % 4000u); break;
    default: break;
    }
}
