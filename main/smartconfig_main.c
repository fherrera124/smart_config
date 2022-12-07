/**
 * @file smartconfig_main.c
 * @author Francisco Herrera (fherrera@lifia.info.unlp.edu.ar)
 * @brief Programa de ejemplo cuya utilidad es conectarse a una red wifi cuando
 * sea posible. Para ello intenta de obtener desde el almacenamiento flash la
 * configuracion de una red almacenada, utilizando el sistema de archivos spiffs.
 * - Si encuentra el archivo: Lo lee y trata de conectarse a la red especificada
 *   en el archivo un máximo de MAXIMUM_RETRY, si no logra conectarse
 *   iniciará smartconfig.
 * - Si no encuentra el archivo: Iniciará smartconfig.
 *
 * Smartconfig: si se encuentra en dicho modo, el dispositivo esperará
 * SMARTCONFIG_WAIT_TICKS para que una persona envie las credenciales usando la
 * aplicacion SmartTouch de espressif.
 *
 * - Si son proporcionadas, dichas credenciales se almacenaran en la memoria flash,
 *   reemplazando las ya existentes, de ser el caso. Luego intentará conectarse.
 * - Si no son proporcionadas a tiempo (timeout), se reinicia el dispositivo y
 *   comienza nuevamente todo el proceso.
 *
 * Siempre que la conexion se pierda, intentará unas MAXIMUM_RETRY veces para
 * volver a conectarse, caso fallido, iniciará smartconfig.
 *
 * @version 0.1
 * @date 2022-12-06
 *
 * @copyright Copyright (c) 2022 Francisco Herrera
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private function prototypes -----------------------------------------------*/
static void conn_task(void* parm);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void initialise_wifi(void);
static void conn_task(void* parm);

/* Extern function definitions -----------------------------------------------*/
extern int wifi_config_read(wifi_config_t* wifi_config);
extern int wifi_config_write(wifi_config_t* wifi_config);
extern int wifi_config_delete();

/* Private macro -------------------------------------------------------------*/
#define MAXIMUM_RETRY          5
#define WAIT_AFTER_RETRY       pdMS_TO_TICKS(5000)
#define SMARTCONFIG_WAIT_TICKS pdMS_TO_TICKS(30000)

/* Private variables ---------------------------------------------------------*/
static const char*   TAG = "smartconfig_example";
static int           s_retry_num = 0;
static bool          s_start_smartconfig = false;
static wifi_config_t wifi_config = { 0 };

static EventGroupHandle_t s_wifi_event_group;
static const int          CONNECTED_BIT = BIT0;
static const int          ESPTOUCH_DONE_BIT = BIT1;

/* Exported functions --------------------------------------------------------*/
void app_main(void)
{
    // initialize NV flash memory storage
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    initialise_wifi();
}

/* Private functions ---------------------------------------------------------*/
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            xTaskCreate(conn_task, "smartconfig", 4096, NULL, 3, NULL);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num++ < MAXIMUM_RETRY) {
                ESP_LOGI(TAG, "retry to connect to the AP");
                esp_wifi_connect();
                vTaskDelay(WAIT_AFTER_RETRY);
                break;
            }
            ESP_LOGE(TAG, "connect to the AP fail, max retries reached. Start smartconfig");
            xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
            s_start_smartconfig = true;
            xTaskCreate(conn_task, "conn_task", 4096, NULL, 3, NULL);
            break;
        }

    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        }

    } else if (event_base == SC_EVENT) {

        switch (event_id) {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "Scan done");
            break;
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "Found channel");
            break;
        case SC_EVENT_GOT_SSID_PSWD:
            ESP_LOGI(TAG, "Got SSID and password");

            smartconfig_event_got_ssid_pswd_t* evt = (smartconfig_event_got_ssid_pswd_t*)event_data;

            memset(&wifi_config, 0, sizeof(wifi_config_t));
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

            wifi_config.sta.bssid_set = evt->bssid_set;
            if (wifi_config.sta.bssid_set == true) {
                memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            }
            wifi_config_write(&wifi_config);
            ESP_LOGI(TAG, "SSID:%s", wifi_config.sta.ssid);
            ESP_LOGI(TAG, "PASSWORD:%s", wifi_config.sta.password);

            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            esp_wifi_connect();
            break;
        case SC_EVENT_SEND_ACK_DONE:
            xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
            break;
        }
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    int err = wifi_config_read(&wifi_config);

    if (err == 0) {
        ESP_LOGD(TAG, "saved ssid: %s saved password: %s\n", wifi_config.sta.ssid, wifi_config.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    } else {
        s_start_smartconfig = true;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void conn_task(void* parm)
{
    if (s_start_smartconfig) {
        ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
        smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    } else {
        esp_wifi_connect();
    }
    EventBits_t uxBits;

    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT,
            true, false, SMARTCONFIG_WAIT_TICKS);
        if (uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
            if (s_start_smartconfig == false) {
                vTaskDelete(NULL);
            }
        } else if (uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        } else {
            ESP_LOGW(TAG, "Timeout waiting for connection. Restarting");
            esp_restart();
        }
    }
}
