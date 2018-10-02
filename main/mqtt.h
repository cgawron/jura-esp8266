#ifndef MQTT_H
#define MQTT_H

#ifdef  __cplusplus
extern "C" {
#endif


extern void mqtt_app_start(void);
extern void mqtt_send(const char *subtopic, const char *template, ...);
extern void mqtt_vsend(const char *subtopic, const char *template, va_list args);

#ifdef  __cplusplus
}
#endif

#endif