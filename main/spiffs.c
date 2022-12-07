/* Includes ------------------------------------------------------------------*/
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_wifi_types.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

/* Private function prototypes -----------------------------------------------*/
int static inline default_init();

/* Private variables ---------------------------------------------------------*/
static const char* TAG = "spiffs_wifi";

/* Exported functions --------------------------------------------------------*/
int wifi_config_read(wifi_config_t* wifi_config)
{
    ESP_LOGI(TAG, "Initializing SPIFFS for read");

    assert(default_init() == 0);

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/wifi_config.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        esp_vfs_spiffs_unregister(NULL);
        return -1;
    }
    fread(wifi_config, sizeof(wifi_config_t), 1, f);
    fclose(f);
    ESP_LOGI(TAG, "SPIFFS unmounted");
    esp_vfs_spiffs_unregister(NULL);
    return 0;
}

int wifi_config_write(wifi_config_t* wifi_config)
{
    ESP_LOGI(TAG, "Initializing SPIFFS for write");

    assert(default_init() == 0);

    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/wifi_config.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        esp_vfs_spiffs_unregister(NULL);
        return -1;
    }
    fwrite(wifi_config, sizeof(wifi_config_t), 1, f);
    fclose(f);

    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
    return 0;
}

int wifi_config_delete()
{
    assert(default_init() == 0);

    int res = unlink("/spiffs/wifi_config.txt");
    if (res == 0) {
        ESP_LOGI(TAG, "File deleted successfully");
    } else {
        ESP_LOGE(TAG, "Error: unable to delete the file");
    }
    esp_vfs_spiffs_unregister(NULL);
    return res;
}

int static inline default_init()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 3,
        .format_if_mount_failed = true
    };

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return -1;
    }
    return 0;
}