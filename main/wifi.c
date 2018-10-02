#include <math.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "lwip/api.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/ip6_addr.h"
#include "esp_spiffs.h"
#include "wifi.h"

static const char *TAG = "wifi";

EventGroupHandle_t connection_event_group;

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_AP_START:
        ESP_LOGI(TAG, "WIFI AP started\n");
        break;

    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;

    case SYSTEM_EVENT_STA_CONNECTED:
        /* enable ipv6 */
        ESP_LOGI(TAG, "STA connected, creating ipv6 link local address");
        tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
        break;

    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(connection_event_group, IPV4_CONNECTED_BIT);
        break;

    case SYSTEM_EVENT_GOT_IP6:
        xEventGroupSetBits(connection_event_group, IPV6_CONNECTED_BIT);
        ip6_addr_t *addr = &event->event_info.got_ip6.ip6_info.ip;
        ESP_LOGI(TAG, "SYSTEM_EVENT_GOT_IP6 address %04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
                 (unsigned int) (IP6_ADDR_BLOCK1(addr)),
                 (unsigned int) (IP6_ADDR_BLOCK2(addr)),
                 (unsigned int) (IP6_ADDR_BLOCK3(addr)),
                 (unsigned int) (IP6_ADDR_BLOCK4(addr)),
                 (unsigned int) (IP6_ADDR_BLOCK7(addr)),
                 (unsigned int) (IP6_ADDR_BLOCK8(addr)));
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(connection_event_group, IPV4_CONNECTED_BIT | IPV6_CONNECTED_BIT);
        break;

    default:
        break;
    }
    return ESP_OK;
}


void app_wifi_init(void)
{
    tcpip_adapter_init();

    ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
    
    // assign a static IP to the network interface
	tcpip_adapter_ip_info_t info;
    memset(&info, 0, sizeof(info));
	IP4_ADDR(&info.ip, 192, 168, 1, 1);
    IP4_ADDR(&info.gw, 192, 168, 1, 1);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
	ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
	ESP_LOGI(TAG, "TCP adapter configured with IP 192.168.1.1/24\n");
	
	// start the DHCP server   
    ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
    ESP_LOGI(TAG, "DHCP server started\n");

    connection_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t sta_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    wifi_config_t ap_config = {
        .ap = {
            .ssid = CONFIG_WIFI_AP_SSID,
            .ssid_len = strlen(CONFIG_WIFI_AP_SSID),
            .max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    // ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", CONFIG_WIFI_SSID, "******");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");

    EventBits_t bits = xEventGroupWaitBits(connection_event_group, IPV4_CONNECTED_BIT, false, true, 60000 / portTICK_PERIOD_MS);

    if (!bits)
    {
        ESP_LOGE(TAG, "Failed to establish STA connection!\n");
    }
}

float get_rssi() {
    wifi_ap_record_t wifidata;
    if (esp_wifi_sta_get_ap_info(&wifidata) == 0)
        return wifidata.rssi;
    else
        return NAN;
}