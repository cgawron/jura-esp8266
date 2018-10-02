#ifndef WIFI_H
#define WIFI_H

#ifdef  __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

extern EventGroupHandle_t connection_event_group;
const static int IPV4_CONNECTED_BIT = BIT0;
const static int IPV6_CONNECTED_BIT = BIT1;
const static int MQTT_CONNECTED_BIT = BIT2;

void app_wifi_init(void);

float get_rssi();

#ifdef  __cplusplus
}
#endif

#endif