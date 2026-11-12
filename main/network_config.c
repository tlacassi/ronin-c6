#include "network_config.h"
#include <string.h>
#include <stdio.h>
#include "esp_check.h"
#include <stdint.h>
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ronin_ble.h"

/* USB is the recovery channel even when credentials or the network are wrong.
   Only this task reads the USB endpoint; credentials never enter the log. */
static portMUX_TYPE gs_sLock = portMUX_INITIALIZER_UNLOCKED;
static bool gs_bReconnect = true;
static int gs_iDisconnectReason = 0;
static esp_netif_t *gs_psStation = NULL;

static void vNETWORK_Event(void *p_pArg, esp_event_base_t p_sBase, int32_t p_i32Id, void *p_pData)
{
    (void)p_pArg;
    if((p_sBase == WIFI_EVENT) && (p_i32Id == WIFI_EVENT_STA_DISCONNECTED))
    {
        const wifi_event_sta_disconnected_t *l_psEvent = p_pData;
        portENTER_CRITICAL(&gs_sLock);
        gs_iDisconnectReason = l_psEvent->reason;
        bool l_bReconnect = gs_bReconnect;
        portEXIT_CRITICAL(&gs_sLock);
        ronin_ble_invalidate();
        if(l_bReconnect)
        {
            (void)esp_wifi_connect();
        }
    }
    else if((p_sBase == WIFI_EVENT) && (p_i32Id == WIFI_EVENT_STA_START))
    {
        wifi_config_t l_sConfig = {0};
        if((esp_wifi_get_config(WIFI_IF_STA, &l_sConfig) == ESP_OK) && (l_sConfig.sta.ssid[0] != 0u))
        {
            (void)esp_wifi_connect();
        }
    }
}

static void vNETWORK_Reply(cJSON *p_psRequest)
{
    const cJSON *l_psId = cJSON_GetObjectItemCaseSensitive(p_psRequest, "id");
    const cJSON *l_psOp = cJSON_GetObjectItemCaseSensitive(p_psRequest, "op");
    if(!cJSON_IsNumber(l_psId) || !cJSON_IsString(l_psOp))
    {
        return;
    }
    cJSON *l_psReply = cJSON_CreateObject();
    if(l_psReply == NULL)
    {
        return;
    }
    cJSON_AddNumberToObject(l_psReply, "id", l_psId->valuedouble);
    bool l_bOk = true;
    const char *l_pcError = "";
    if(strcmp(l_psOp->valuestring, "wifi.set") == 0)
    {
        const cJSON *l_psSSID = cJSON_GetObjectItemCaseSensitive(p_psRequest, "ssid");
        const cJSON *l_psPassword = cJSON_GetObjectItemCaseSensitive(p_psRequest, "password");
        size_t l_uSSIDLength = cJSON_IsString(l_psSSID) ? strlen(l_psSSID->valuestring) : 0u;
        size_t l_uPasswordLength = cJSON_IsString(l_psPassword) ? strlen(l_psPassword->valuestring) : 65u;
        if((l_uSSIDLength == 0u) || (l_uSSIDLength > 32u) || (l_uPasswordLength > 63u) ||
           ((l_uPasswordLength != 0u) && (l_uPasswordLength < 8u)))
        {
            l_bOk = false;
            l_pcError = "invalid_credentials";
        }
        else
        {
            wifi_config_t l_sConfig = {0};
            memcpy(l_sConfig.sta.ssid, l_psSSID->valuestring, l_uSSIDLength);
            memcpy(l_sConfig.sta.password, l_psPassword->valuestring, l_uPasswordLength);
            portENTER_CRITICAL(&gs_sLock);
            gs_bReconnect = false;
            gs_iDisconnectReason = 0;
            portEXIT_CRITICAL(&gs_sLock);
            ronin_ble_invalidate();
            esp_err_t l_eResult = esp_wifi_stop();
            if(l_eResult == ESP_OK)
            {
                l_eResult = esp_wifi_set_config(WIFI_IF_STA, &l_sConfig);
            }
            memset(&l_sConfig, 0, sizeof(l_sConfig));
            portENTER_CRITICAL(&gs_sLock);
            gs_bReconnect = true;
            portEXIT_CRITICAL(&gs_sLock);
            esp_err_t l_eStart = esp_wifi_start();
            l_bOk = (l_eResult == ESP_OK) && (l_eStart == ESP_OK);
            if(!l_bOk)
            {
                l_pcError = "wifi_storage_or_start_failed";
            }
        }
    }
    else if(strcmp(l_psOp->valuestring, "wifi.status") != 0)
    {
        l_bOk = false;
        l_pcError = "unsupported_operation";
    }
    wifi_config_t l_sCurrent = {0};
    if(esp_wifi_get_config(WIFI_IF_STA, &l_sCurrent) == ESP_OK)
    {
        char l_acSSID[33] = {0};
        memcpy(l_acSSID, l_sCurrent.sta.ssid, 32u);
        cJSON_AddStringToObject(l_psReply, "ssid", l_acSSID);
        memset(&l_sCurrent, 0, sizeof(l_sCurrent));
    }
    esp_netif_ip_info_t l_sIP = {0};
    wifi_ap_record_t l_sAP = {0};
    bool l_bConnected = (esp_wifi_sta_get_ap_info(&l_sAP) == ESP_OK) &&
                       (esp_netif_get_ip_info(gs_psStation, &l_sIP) == ESP_OK) && (l_sIP.ip.addr != 0u);
    char l_acIP[16] = {0};
    if(l_bConnected)
    {
        snprintf(l_acIP, sizeof(l_acIP), IPSTR, IP2STR(&l_sIP.ip));
    }
    portENTER_CRITICAL(&gs_sLock);
    int l_iReason = gs_iDisconnectReason;
    portEXIT_CRITICAL(&gs_sLock);
    cJSON_AddBoolToObject(l_psReply, "ok", l_bOk);
    cJSON_AddBoolToObject(l_psReply, "connected", l_bConnected);
    cJSON_AddStringToObject(l_psReply, "ip", l_acIP);
    cJSON_AddNumberToObject(l_psReply, "reason", l_iReason);
    cJSON_AddStringToObject(l_psReply, "error", l_pcError);
    char *l_pcBody = cJSON_PrintUnformatted(l_psReply);
    if(l_pcBody != NULL)
    {
        char l_acLine[512] = {0};
        int l_iLength = snprintf(l_acLine, sizeof(l_acLine), "RONIN %s\n", l_pcBody);
        if((l_iLength > 0) && ((size_t)l_iLength < sizeof(l_acLine)))
        {
            /* Keep TX on the SDK console path. The RX interrupt driver is
               independent; stdout serializes replies with diagnostic output. */
            (void)fwrite(l_acLine, 1u, (size_t)l_iLength, stdout);
            (void)fflush(stdout);
        }
        cJSON_free(l_pcBody);
    }
    cJSON_Delete(l_psReply);
}

static void vNETWORK_USBTask(void *p_pArg)
{
    (void)p_pArg;
    char l_acLine[512] = {0};
    size_t l_uUsed = 0u;
    bool l_bOverflow = false;
    for(;;)
    {
        uint8_t l_u8Byte = 0u;
        if(usb_serial_jtag_read_bytes(&l_u8Byte, 1u, pdMS_TO_TICKS(100)) != 1)
        {
            continue;
        }
        if(l_u8Byte == '\n')
        {
            if(!l_bOverflow && (l_uUsed > 6u) && (memcmp(l_acLine, "RONIN ", 6u) == 0) &&
               (strstr(l_acLine, "\\u0000") == NULL))
            {
                const char *l_pcEnd = NULL;
                cJSON *l_psRequest = cJSON_ParseWithOpts(l_acLine + 6u, &l_pcEnd, true);
                if(cJSON_IsObject(l_psRequest))
                {
                    vNETWORK_Reply(l_psRequest);
                }
                cJSON_Delete(l_psRequest);
            }
            memset(l_acLine, 0, sizeof(l_acLine));
            l_uUsed = 0u; l_bOverflow = false;
        }
        else if(!l_bOverflow)
        {
            if((l_u8Byte == 0u) || (l_uUsed >= sizeof(l_acLine) - 1u))
            {
                l_bOverflow = true;
            }
            else
            {
                l_acLine[l_uUsed++] = (char)l_u8Byte;
            }
        }
    }
}

esp_err_t network_config_start(void)
{
    gs_psStation = esp_netif_create_default_wifi_sta();
    if(gs_psStation == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t l_sInit = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&l_sInit), "network", "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), "network", "Wi-Fi storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, vNETWORK_Event, NULL), "network", "Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), "network", "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), "network", "Wi-Fi start failed");
    usb_serial_jtag_driver_config_t l_sUSB = {.tx_buffer_size = 1024u, .rx_buffer_size = 1024u};
    ESP_RETURN_ON_ERROR(usb_serial_jtag_driver_install(&l_sUSB), "network", "USB init failed");
    if(xTaskCreate(vNETWORK_USBTask, "wifi_usb", 4096u, NULL, 4u, NULL) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
