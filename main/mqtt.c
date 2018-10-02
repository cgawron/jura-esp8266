#include <stdarg.h>
#include <time.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "wifi.h"
#include "cron.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "main.h"

static const char *TAG = "mqtt";

const esp_partition_t *update_partition = NULL;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static char chipid[16];

// external functions & variables
extern EventGroupHandle_t connection_event_group;
extern void set_pump(int pump, int status);

void mqtt_subscribe(const char *subtopic)
{
    char topic[256];
    int msg_id;
    snprintf(topic, 255, "%s/%s", chipid, subtopic);
    msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, 0);
    ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
}

void mqtt_send(const char *subtopic, const char *template, ...)
{
    if (!mqtt_client) 
        return;

    char topic[256];
    char message[256];
    memset(message, 0, 255);
    va_list ap;
    va_start(ap, template);
    vsnprintf(message, 255, template, ap);
    va_end(ap);

    snprintf(topic, 255, "%s/%s", chipid, subtopic);
    esp_mqtt_client_publish(mqtt_client, topic, message, strlen(message), 0, 1);
}

void mqtt_vsend(const char *subtopic, const char *template, va_list args)
{
    if (!mqtt_client) 
        return;
        
    char topic[256];
    char message[256];
    memset(message, 0, 255);
    vsnprintf(message, 255, template, args);

    snprintf(topic, 255, "%s/%s", chipid, subtopic);
    esp_mqtt_client_publish(mqtt_client, topic, message, strlen(message), 0, 0);
}



static void handle_ota(esp_mqtt_event_handle_t event, char *path[], int numComponents)
{
    esp_err_t err;
    static esp_ota_handle_t update_handle = 0;

    if (numComponents < 3)
    {
        ESP_LOGW(TAG, "too few components");
        return;
    }
    if (strcmp("version", path[2]) == 0)
    {
        ESP_LOGI(TAG, "ota: version=%.*s", event->data_len, event->data);
        if (strncmp(BUILD_TAG, event->data, strlen(BUILD_TAG)) < 0)
        {
            ESP_LOGI(TAG, "ota: firmware outdated, requesting update");
            mqtt_subscribe("ota/firmware");
        }
    }
    else if (strcmp("firmware", path[2]) == 0)
    {
        ESP_LOGI(TAG, "ota: receiving firmware image");
        if (event->current_data_offset == 0)
        {
            update_partition = esp_ota_get_next_update_partition(NULL);
            ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
                     update_partition->subtype, update_partition->address);
            assert(update_partition != NULL);

            err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                return;
            }
            ESP_LOGI(TAG, "esp_ota_begin succeeded");
        }
        if (update_handle)
        {
            // ESP_LOGI(TAG, "ota: writing %d bytes at offset %d", event->data_len, event->current_data_offset);
            err = esp_ota_write(update_handle, event->data, event->data_len);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Error: esp_ota_write failed (%s)!", esp_err_to_name(err));
                return;
            }
        }
        if (event->data_len + event->current_data_offset >= event->total_data_len)
        {
            if (esp_ota_end(update_handle) != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_ota_end failed!");
                return;
            }
            err = esp_ota_set_boot_partition(update_partition);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
                return;
            }
            ESP_LOGI(TAG, "Prepare to restart system!");
            esp_restart();
            return;
        }
    }
}

static void handle_mqtt_message(esp_mqtt_event_handle_t event)
{
    int i;
    char *last = 0;
    static char *path[16];
    static char topic[256];
    static int numComp;

    if (event->current_data_offset == 0)
    {
        memset(topic, 0, 256);
        strncpy(topic, event->topic, event->topic_len > 255 ? 255 : event->topic_len);
        ESP_LOGI(TAG, "topic=%s", topic);
        ESP_LOGI(TAG, "data length=%d total=%d offset=%d", event->data_len, event->total_data_len, event->current_data_offset);

        for (i = 0, path[i] = strtok_r(topic, "/", &last); i < 15 && path[i] != 0; path[++i] = strtok_r(0, "/", &last))
        {
            if (path[i])
            {
                ESP_LOGI(TAG, "path[%d] = %s", i, path[i]);
            }
        }
        numComp = i;
        if (numComp < 2)
        {
            ESP_LOGW(TAG, "too few components");
            return;
        }
    }

    if (strcmp("ota", path[1]) == 0)
    {
        handle_ota(event, path, numComp);
    }
    else if (strcmp("config", path[1]) == 0)
    {
    }
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;

    // your_context_t *context = event->context;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_client = client;

        // enable logging via mqtt
        xEventGroupSetBits(connection_event_group, MQTT_CONNECTED_BIT);

        mqtt_subscribe("pump/#");
        mqtt_subscribe("config/#");
        mqtt_subscribe("ota/version");

        break;

    case MQTT_EVENT_DISCONNECTED:
        xEventGroupClearBits(connection_event_group, MQTT_CONNECTED_BIT);

        mqtt_client = NULL;
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        handle_mqtt_message(event);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    }
    return ESP_OK;
}

void mqtt_app_start(void)
{

    uint8_t _chipid[6];
    esp_efuse_mac_get_default(_chipid);
    sprintf(chipid, "%02x%02x%02x%02x%02x%02x",
            _chipid[0], _chipid[1], _chipid[2], _chipid[3], _chipid[4], _chipid[5]);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_MQTT_URI,
        .event_handle = mqtt_event_handler,
        // .user_context = (void *)your_context
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);

    xEventGroupWaitBits(connection_event_group, MQTT_CONNECTED_BIT, false, true, 60000 / portTICK_PERIOD_MS);
}