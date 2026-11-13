#include "status_led.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "led_strip.h"
#include "network_config.h"
#include "ronin_ble.h"
#include "status_led_pattern.h"

#define BOOT_MS 4000u
#define NVS_NAMESPACE "ronin"
#define NVS_KEY "led_pct"

static volatile uint8_t gs_u8Brightness = CONFIG_RONIN_LED_BRIGHTNESS;

uint8_t status_led_brightness(void)
{
    return gs_u8Brightness;
}

bool status_led_set_brightness(uint8_t percent)
{
    if(percent > 100u) { return false; }
    gs_u8Brightness = percent;
    nvs_handle_t l_hStore;
    if(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &l_hStore) != ESP_OK) { return false; }
    esp_err_t l_eResult = nvs_set_u8(l_hStore, NVS_KEY, percent);
    if(l_eResult == ESP_OK) { l_eResult = nvs_commit(l_hStore); }
    nvs_close(l_hStore);
    return l_eResult == ESP_OK;
}

static void vSTATUSLED_Load(void)
{
    nvs_handle_t l_hStore;
    uint8_t l_u8Value = 0u;
    if(nvs_open(NVS_NAMESPACE, NVS_READONLY, &l_hStore) != ESP_OK) { return; }
    if((nvs_get_u8(l_hStore, NVS_KEY, &l_u8Value) == ESP_OK) && (l_u8Value <= 100u)) { gs_u8Brightness = l_u8Value; }
    nvs_close(l_hStore);
}

static void vSTATUSLED_Task(void *p_pArg)
{
    led_strip_handle_t l_hStrip = (led_strip_handle_t)p_pArg;
    for(;;)
    {
        uint32_t l_u32Now = (uint32_t)(esp_timer_get_time() / 1000);
        status_led_state_t l_eState;
        if(l_u32Now < BOOT_MS) { l_eState = STATUS_LED_BOOT; }
        else if(!network_config_connected()) { l_eState = STATUS_LED_NO_WIFI; }
        else if(!ronin_ble_ready()) { l_eState = STATUS_LED_NO_RONIN; }
        else { l_eState = STATUS_LED_OK; }
        uint8_t l_au8RGB[3];
        status_led_pattern(l_eState, l_u32Now, l_au8RGB);
        uint32_t l_u32Percent = gs_u8Brightness;
        (void)led_strip_set_pixel(l_hStrip, 0u, (l_au8RGB[0] * l_u32Percent) / 100u,
                                  (l_au8RGB[1] * l_u32Percent) / 100u, (l_au8RGB[2] * l_u32Percent) / 100u);
        (void)led_strip_refresh(l_hStrip);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

void status_led_start(void)
{
    vSTATUSLED_Load();
    if(CONFIG_RONIN_LED_GPIO < 0) { return; }
    led_strip_config_t l_sStrip = {.strip_gpio_num = CONFIG_RONIN_LED_GPIO, .max_leds = 1, .led_model = LED_MODEL_WS2812,
                                   .led_pixel_format = LED_PIXEL_FORMAT_GRB};
    led_strip_rmt_config_t l_sRMT = {.clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10u * 1000u * 1000u};
    led_strip_handle_t l_hStrip = NULL;
    if(led_strip_new_rmt_device(&l_sStrip, &l_sRMT, &l_hStrip) != ESP_OK)
    { ESP_LOGW("led", "Status LED unavailable"); return; }
    if(xTaskCreate(vSTATUSLED_Task, "status_led", 3072u, l_hStrip, 1u, NULL) != pdPASS)
    { ESP_LOGW("led", "Status LED task could not start"); }
}
