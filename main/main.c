#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event_loop.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

// #include "soc/rtc.h"

#include "apps/sntp/sntp.h"

#include "main.h"
#include "mqtt.h"
#include "wifi.h"

#ifndef BUILD
#define BUILD "_unknown_build_"
#endif

static const char *TAG = "jura";

const char *BUILD_TAG = BUILD;
const int wakeup_time_sec = 60;

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, (char *)CONFIG_NTP_SERVER);
    sntp_init();
}

static void obtain_time(void)
{
    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

void init_nvs()
{
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void mainLoop()
{
    ESP_LOGI(TAG, "main loop task starting");
    obtain_time();
    mqtt_app_start();

    for (;;)
    {
        EventBits_t bits = xEventGroupGetBits(connection_event_group);
        if (bits & MQTT_CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "Sending time");
            time_t now;
            time(&now);
            mqtt_send("time", "%s", ctime(&now));

            ESP_LOGI(TAG, "Sending rssi");
            mqtt_send("rssi", "%.1f", get_rssi());

            ESP_LOGI(TAG, "Sending firmware version");
            mqtt_send("version", "%s", BUILD_TAG);

            vTaskDelay(60000 / portTICK_PERIOD_MS);
        }
        else
        {
            ESP_LOGE(TAG, "reconnecting to MQTT\n");
            mqtt_app_start();
        }
    }
}

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "[APP] BUILD: %s", BUILD_TAG);

    esp_log_level_set("*", ESP_LOG_VERBOSE);
    esp_log_level_set("http_server", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    init_nvs();
    app_wifi_init();
    // init_logging();

    TaskHandle_t xHandle = NULL;

    ESP_LOGI(TAG, "creating main task");

    xTaskCreate(mainLoop,         /* Function that implements the task. */
                "MainLoop",       /* Text name for the task. */
                4096,             /* Stack size in words, not bytes. */
                (void *)NULL,     /* Parameter passed into the task. */
                tskIDLE_PRIORITY, /* Priority at which the task is created. */
                &xHandle);
    // ESP_LOGI(TAG, "starting scheduler");
    // vTaskStartScheduler();
}