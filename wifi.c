#include "wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#ifdef CONFIG_IDF_TARGET_ESP8266
#include "esp_netif.h"
#include "esp_event_loop.h"
#endif
#include "esp_event.h"
#include "esp_log.h"
#include "string.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "sntp.h"
#include "time.h"

#define WIFI_MAXIMUM_RETRY 5

static const char *TAG = "wifi";
static wifi_manager_t s_wifi_manager;
static wifi_callbacks_t *s_wifi_callbacks = NULL;

void wifi_handle_connecting();
void wifi_handle_disconnect(wifi_event_sta_disconnected_t *event_data);
void wifi_handle_connect(ip_event_got_ip_t *event_data);
void wifi_handler_reconnect(wifi_event_sta_disconnected_t *event_data);
void wifi_handle_connect_fail(wifi_event_sta_disconnected_t *event_data);

void wifi_handle_connecting()
{
  ESP_LOGI(TAG, "Connecting to WIFI");
  s_wifi_manager.retries = 0;
  s_wifi_manager.state = WIFI_STATE_CONNECTING;
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_CONNECTING, NULL);
  }
  esp_wifi_connect();
}

void wifi_handle_disconnect(wifi_event_sta_disconnected_t *event_data)
{

  s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
  s_wifi_manager.retries = 0;

  ESP_LOGI(TAG, "WIFI disconnected (Reason: %i)", event_data->reason);
  wifi_disconnect();

  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_DISCONNECTED, event_data);
  }
}

void wifi_handle_connect(ip_event_got_ip_t *event_data)
{
  // ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  s_wifi_manager.state = WIFI_STATE_CONNECTED;
  // memcpy(&s_wifi_manager.ip, &event->ip_info.ip, sizeof(esp_ip_addr_t));
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_CONNECTED, event_data);
  }
  s_wifi_manager.retries = 0;
}

void wifi_handler_reconnect(wifi_event_sta_disconnected_t *event_data)
{
  ESP_LOGI(TAG, "Connection to WIFI failed. Trying %i more times. (Reason: %i)", WIFI_MAXIMUM_RETRY - s_wifi_manager.retries, event_data->reason);
  s_wifi_manager.retries += 1;
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_RETRYING, event_data);
  }
  esp_wifi_connect();
}

void wifi_handle_connect_fail(wifi_event_sta_disconnected_t *event_data)
{
  wifi_disconnect();
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_CONNECT_FAIL, event_data);
  }
  ESP_LOGI(TAG, "Connection to WIFI failed %i times. Giving up. (Reason: %i)", WIFI_MAXIMUM_RETRY, event_data->reason);
}

void wifi_handle_ap_start()
{
  s_wifi_manager.state = WIFI_STATE_AP_STARTED;
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_AP_STARTED, NULL);
  }
}

void wifi_handle_ap_stop()
{
  s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_AP_STOPPED, NULL);
  }
}

void wifi_handle_ap_connected(wifi_event_ap_staconnected_t *event_data)
{
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_AP_CONNECTED, NULL);
  }
}

void wifi_handle_ap_disconnected(wifi_event_ap_stadisconnected_t *event_data)
{
  if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
  {
    s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_AP_DISCONNECTED, NULL);
  }
}

#ifdef CONFIG_IDF_TARGET_ESP8266
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
  int32_t event_id = event->event_id;

  if (event_id == SYSTEM_EVENT_STA_START)
  {
    wifi_handle_connecting()
  }
  else if (event_id == SYSTEM_EVENT_STA_DISCONNECTED)
  {
    if (s_wifi_manager.state != WIFI_STATE_CONNECTED && s_wifi_manager.desired_state == WIFI_STATE_CONNECTED && s_wifi_manager.retries < WIFI_MAXIMUM_RETRY)
    {
      wifi_handler_reconnect(event_data);
    }
    else if (s_wifi_manager.desired_state == WIFI_STATE_CONNECTED && s_wifi_manager.retries >= WIFI_MAXIMUM_RETRY)
    {
      wifi_handle_connect_fail(event_data);
    }
    else
    {
      wifi_handle_disconnect((wifi_event_sta_disconnected_t *)event->event_data)
    }
  }
  else if (event_id == SYSTEM_EVENT_STA_GOT_IP)
  {
    wifi_handle_connect((ip_event_got_ip_t *)event->event_data)
  }
  return ESP_OK;
}
#else
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
  {
    wifi_handle_connecting();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    wifi_handle_disconnect((wifi_event_sta_disconnected_t *)event_data);
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
  {
    wifi_handle_ap_start();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP)
  {
    wifi_handle_ap_stop();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
  {
    wifi_handle_ap_connected((wifi_event_ap_staconnected_t *)event_data);
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
  {
    wifi_handle_ap_disconnected((wifi_event_ap_stadisconnected_t *)event_data);
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    wifi_handle_connect((ip_event_got_ip_t *)event_data);
  }
}
#endif

void wifi_init(wifi_callbacks_t *callbacks)
{
  ESP_LOGI(TAG, "Initializing callbacks");
  s_wifi_callbacks = callbacks;
  s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
  s_wifi_manager.desired_state = WIFI_STATE_DISCONNECTED;
  s_wifi_manager.retries = 0;

#ifdef CONFIG_IDF_TARGET_ESP8266
  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
#else
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

#ifndef CONFIG_IDF_TARGET_ESP8266
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
#endif
}

void wifi_connect_ssid(const char *ssid, const char *password)
{
  ESP_LOGI(TAG, "Connecting WIFI to SSID %s", ssid);
  if (s_wifi_manager.state == WIFI_STATE_CONNECTED || s_wifi_manager.state == WIFI_STATE_AP_STARTED)
  {
    wifi_disconnect();
  }

  s_wifi_manager.desired_state = WIFI_STATE_CONNECTED;

  wifi_config_t wifi_config = {};
  strncpy((char *)&(wifi_config.sta.ssid), ssid, 32);
  strncpy((char *)&(wifi_config.sta.password), password, 64);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
#ifndef CONFIG_IDF_TARGET_ESP8266
  esp_netif_create_default_wifi_sta();
#endif
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WIFI initialisation complete.");
}

void wifi_start_soft_ap(const char *ssid, const char *password)
{
  ESP_LOGI(TAG, "Creating a software access point with SSID %s", ssid);
  if (s_wifi_manager.state == WIFI_STATE_CONNECTED || s_wifi_manager.state == WIFI_STATE_AP_STARTED)
  {
    wifi_disconnect();
  }

  s_wifi_manager.desired_state = WIFI_STATE_AP_STARTED;

  wifi_config_t wifi_config = {};
  strncpy((char *)&(wifi_config.ap.ssid), ssid, 32);
  strncpy((char *)&(wifi_config.ap.password), password, 64);

  if (strlen(password) == 0)
  {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  }
  else
  {
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  }

  wifi_config.ap.max_connection = 4; // TODO: Make config.

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
#ifndef CONFIG_IDF_TARGET_ESP8266
  esp_netif_create_default_wifi_ap();
#endif
  ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_stop_soft_ap()
{
  s_wifi_manager.desired_state = WIFI_STATE_DISCONNECTED;
  esp_err_t result = esp_wifi_disconnect();
  switch (result)
  {
  case ESP_OK:
    s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
    s_wifi_manager.retries = 0;
    // s_wifi_manager.addr = IPADDR_NONE;
    ESP_LOGI(TAG, "Software AP stopped");
    break;
  default:
    if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
    {
      s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_AP_STOPPED, NULL);
    }
    ESP_LOGE(TAG, "Unable to stop software AP: %i", result);
  }

  ESP_ERROR_CHECK(esp_wifi_stop());
}

void wifi_disconnect()
{

  s_wifi_manager.desired_state = WIFI_STATE_DISCONNECTED;
  esp_err_t result = esp_wifi_disconnect();
  switch (result)
  {
  case ESP_OK:
    s_wifi_manager.state = WIFI_STATE_DISCONNECTED;
    s_wifi_manager.retries = 0;
    // s_wifi_manager.addr = IPADDR_NONE;
    ESP_LOGI(TAG, "WIFI Disconnected");
    break;
  default:
    if (s_wifi_callbacks != NULL && s_wifi_callbacks->on_event != NULL)
    {
      s_wifi_callbacks->on_event(&s_wifi_manager, WIFI_DISCONNECT_FAIL, NULL);
    }
    ESP_LOGE(TAG, "WIFI Disconnect failed: %i", result);
  }

  ESP_ERROR_CHECK(esp_wifi_stop());
}

void wifi_uninit()
{
#ifndef CONFIG_IDF_TARGET_ESP8266
  ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
  ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
#endif
}

wifi_manager_state_t wifi_get_state()
{
  return s_wifi_manager.state;
}