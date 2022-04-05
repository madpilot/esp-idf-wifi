#ifndef _PROVIDORE_WIFI_SERVICE_h
#define _PROVIDORE_WIFI_SERVICE_h
#include "esp_wifi.h"
#include "freertos/event_groups.h"

typedef void (*wifi_on_connect)();
typedef struct _wifi_callbacks_t
{
  wifi_on_connect on_connect;
} wifi_callbacks_t;

void wifi_init(wifi_callbacks_t *callbacks);
void wifi_init_sta(const char *ssid, const char *password);
#endif