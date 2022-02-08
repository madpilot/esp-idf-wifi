#include "wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
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

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char *TAG = "wifi";

static int s_retry_num = 0;

#ifdef CONFIG_IDF_TARGET_ESP8266
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
  int32_t event_id = event->event_id;

  if (event_id == SYSTEM_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_id == SYSTEM_EVENT_STA_DISCONNECTED)
  {
    if (s_retry_num < WIFI_MAXIMUM_RETRY)
    {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Connection to AP failed. Retrying.");
    }
    else
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "Unable to connect to the AP");
  }
  else if (event_id == SYSTEM_EVENT_STA_GOT_IP)
  {
    ESP_LOGI(TAG, "Received IP Address: %s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
  return ESP_OK;
}
#else
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_id == WIFI_EVENT_STA_START)
  {
    esp_wifi_connect();
  }
  else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
  {
    if (s_retry_num < WIFI_MAXIMUM_RETRY)
    {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Connection to AP failed. Retrying.");
    }
    else
    {
      xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGI(TAG, "Unable to connect to the AP");
  }
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Received IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    s_retry_num = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

#ifdef CONFIG_MADPILOT_USE_NTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CONFIG_MADPILOT_NTP_SERVER);
    sntp_init();
    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
    {
      ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS)
    {
      ESP_LOGI(TAG, "In progress");
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    time(&now);
    localtime_r(&now, &timeinfo);
#endif
  }
}
#endif

void wifi_init_sta(const char *ssid, const char *password)
{
  ESP_LOGI(TAG, "Starting WIFI initialisation.");
  s_wifi_event_group = xEventGroupCreate();

#ifdef CONFIG_IDF_TARGET_ESP8266
  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
#else
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
#endif

#ifndef CONFIG_IDF_TARGET_ESP8266
  esp_netif_create_default_wifi_sta();
#endif

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

#ifndef CONFIG_IDF_TARGET_ESP8266
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
#endif

  wifi_config_t wifi_config = {};
  strncpy((char *)&(wifi_config.sta.ssid), ssid, 32);
  strncpy((char *)&(wifi_config.sta.password), password, 64);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "WIFI initialisation complete.");

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
   * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
   * happened. */
  if (bits & WIFI_CONNECTED_BIT)
  {
    ESP_LOGI(TAG, "Connected to AP SSID: %s", ssid);
  }
  else if (bits & WIFI_FAIL_BIT)
  {
    ESP_LOGI(TAG, "Failed to connect to SSID: %s", ssid);
  }
  else
  {
    ESP_LOGE(TAG, "UNEXPECTED EVENT");
  }

#ifndef CONFIG_IDF_TARGET_ESP8266
  ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler));
  ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler));
#endif
  vEventGroupDelete(s_wifi_event_group);
}
