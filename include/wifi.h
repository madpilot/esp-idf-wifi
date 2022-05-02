#ifndef _PROVIDORE_WIFI_MANAGER_h
#define _PROVIDORE_WIFI_MANAGER_h
#include "esp_wifi.h"
#include "freertos/event_groups.h"

typedef enum _wifi_manager_state
{
  WIFI_STATE_DISCONNECTED,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED
} wifi_manager_state_t;

typedef enum _wifi_events
{
  WIFI_CONNECTING,
  WIFI_RETRYING,
  WIFI_CONNECTED,
  WIFI_CONNECT_FAIL,
  WIFI_DISCONNECTED,
  WIFI_DISCONNECT_FAIL
} wifi_events_t;

typedef struct _wifi_manager
{
  wifi_manager_state_t state;
  wifi_manager_state_t desired_state;
  uint8_t retries;
  esp_ip_addr_t ip;
} wifi_manager_t;

typedef void (*wifi_event_listener)(wifi_manager_t *wifi_manager, wifi_events_t event, void *event_data);
typedef struct _wifi_callbacks_t
{
  wifi_event_listener on_event;
} wifi_callbacks_t;

void wifi_init(wifi_callbacks_t *callbacks);
void wifi_connect_ssid(const char *ssid, const char *password);
void wifi_disconnect();
void wifi_uninit();

wifi_manager_state_t wifi_get_state();
#endif