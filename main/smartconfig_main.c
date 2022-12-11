/**
 * @file smartconfig_main.c
 * @author Francisco Herrera (fherrera@lifia.info.unlp.edu.ar)
 * @brief Programa de ejemplo cuya utilidad es conectarse a una red wifi cuando
 * sea posible. Para ello intenta de obtener desde el almacenamiento flash la
 * configuracion de una red almacenada, utilizando el sistema de archivos spiffs.
 * - Si encuentra el archivo: Lo lee y trata de conectarse a la red especificada
 *   en el archivo un número máximo especificado de intentos. Si no lo logra
 *   iniciará smartconfig.
 * - Si no encuentra el archivo: Iniciará smartconfig.
 *
 * Smartconfig: si se encuentra en dicho modo, el dispositivo esperará un
 * tiempo determinado por credenciales desde la aplicacion SmartTouch de espressif.
 *
 * - Si son proporcionadas: intentará conectarse.
 *   - Si logra conectarse: dichas credenciales se almacenarán en la memoria
 *     flash, reemplazando las anteriores, en caso de existir.
 *   - Si no logra conectarse: luego de alcanzar el número máximo de intentos,
 *     reiniciará.
 *
 * - Si no son proporcionadas (timeout): se reinicia el dispositivo y comienza
 *   nuevamente todo el proceso.
 *
 * Nota: Siempre que la conexion se pierda, intentará volver a conectarse un
 * máximo especificado de intentos. Si no lo logra iniciará smartconfig.
 *
 * @version 0.1
 * @date 2022-12-06
 *
 * @copyright Copyright (c) 2022 Francisco Herrera
 *
 */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "spiffs_wifi.h"

/* Private function prototypes -----------------------------------------------*/
static void smartconfig_task(void* parm);
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void initialise_wifi(void);

/* Private macro -------------------------------------------------------------*/
#define MAXIMUM_RETRY    3
#define WAIT_AFTER_RETRY pdMS_TO_TICKS(5000) // WAIT_AFTER_RETRY must be smaller than WAIT_FOR_EVENT
#define WAIT_FOR_EVENT   pdMS_TO_TICKS(40000)
#define NOTIFY_TASK(event) \
    if (smartconfig_mode)  \
        xTaskNotify(task_handle, event, eSetValueWithOverwrite);

/* Private variables ---------------------------------------------------------*/
static const char*   TAG = "smartconfig_example";
static int           retry_num = 0;
static bool          smartconfig_mode = false;
static wifi_config_t wifi_config = { 0 };
static TaskHandle_t  task_handle = NULL;

static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const int RETRIED_BIT = BIT2;

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
            smartconfig_mode ? xTaskCreate(smartconfig_task, "smartconfig", 4096, NULL, 3, &task_handle)
                             : esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (retry_num++ < MAXIMUM_RETRY) {
                ESP_LOGI(TAG, "retry to connect to the AP");
                esp_wifi_connect();
                vTaskDelay(WAIT_AFTER_RETRY);
                NOTIFY_TASK(RETRIED_BIT);
                break;
            }
            if (smartconfig_mode) {
                ESP_LOGE(TAG, "Failed smartconfig. Wrong credentials or AP unreachable. Restarting");
                esp_restart();
            };
            smartconfig_mode = true;
            ESP_LOGE(TAG, "Wrong credentials or AP unreachable. Start smartconfig");
            xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, &task_handle);
            break;
        }

    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            retry_num = 0;
            NOTIFY_TASK(CONNECTED_BIT);
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
            if (wifi_config.sta.bssid_set) {
                memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
            }

            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            esp_wifi_connect();
            break;
        case SC_EVENT_SEND_ACK_DONE:
            NOTIFY_TASK(ESPTOUCH_DONE_BIT);
            break;
        }
    }
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (CONFIG_FOUND == wifi_config_read(&wifi_config)) {
        ESP_LOGI(TAG, "Recovered credentials from flash memory");
        ESP_LOGD(TAG, "saved ssid: %s saved password: %s\n", wifi_config.sta.ssid, wifi_config.sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    } else {
        smartconfig_mode = true;
    }
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void smartconfig_task(void* parm)
{
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    uint32_t notif;

    do {
        switch (notif) {
        case CONNECTED_BIT:
            ESP_LOGI(TAG, "WiFi Connected to ap");
            wifi_config_write(&wifi_config);
            break;
        case ESPTOUCH_DONE_BIT:
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            smartconfig_mode = false;
            vTaskDelete(NULL);
            return;
        case RETRIED_BIT:
            break;
        }
    } while (xTaskNotifyWait(pdFALSE, ULONG_MAX, &notif, WAIT_FOR_EVENT));
    ESP_LOGW(TAG, "Timeout waiting for connection. Restarting");
    esp_restart();
}
