#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
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

#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

static const char *TAG = "GARAGE";
static const char *LEFT_DOOR_TOPIC = "garage/door/left";
static const char *RIGHT_DOOR_TOPIC = "garage/door/right";

static const char *LEFT_DOOR_STATUS_TOPIC = "garage/door/left/status";
static const char *RIGHT_DOOR_STATUS_TOPIC = "garage/door/right/status";

static EventGroupHandle_t wifi_event_group;
const static int CONNECTED_BIT = BIT0;

#define LEFT_DOOR_GPIO 32
#define RIGHT_DOOR_GPIO 33

#define LEFT_DOOR_SENSOR_GPIO 25
#define RIGHT_DOOR_SENSOR_GPIO 26

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            msg_id = esp_mqtt_client_subscribe(client, LEFT_DOOR_TOPIC, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, RIGHT_DOOR_TOPIC, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, LEFT_DOOR_STATUS_TOPIC, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, RIGHT_DOOR_STATUS_TOPIC, 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            break;
        case MQTT_EVENT_DISCONNECTED:
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
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            // Get the topic name
            char *topic = malloc(event->topic_len + 1);
            memcpy(topic, event->topic, event->topic_len);
            topic[event->topic_len] = '\0';

            // Get the data
            char *data = malloc(event->data_len + 1);
            memcpy(data, event->data, event->data_len);
            data[event->data_len] = '\0';

            ESP_LOGI(TAG, "data: %s", data);

            if (strcmp(topic, LEFT_DOOR_TOPIC) == 0) {
                if (strcmp(data, "push") == 0) {
                    gpio_set_level(LEFT_DOOR_GPIO, 0);
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    gpio_set_level(LEFT_DOOR_GPIO, 1);
                }

            } else if (strcmp(topic, RIGHT_DOOR_TOPIC) == 0) {
                if (strcmp(data, "push") == 0) {
                    gpio_set_level(RIGHT_DOOR_GPIO, 0);
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    gpio_set_level(RIGHT_DOOR_GPIO, 1);
                } 
            } else if (strcmp(topic, LEFT_DOOR_STATUS_TOPIC) == 0) {
                if (strcmp(data, "get") == 0) {
                    if (gpio_get_level(LEFT_DOOR_SENSOR_GPIO)) {
                        esp_mqtt_client_publish(client, LEFT_DOOR_STATUS_TOPIC, "status:open", 0, 0, 0);
                    } else {
                        esp_mqtt_client_publish(client, LEFT_DOOR_STATUS_TOPIC, "status:closed", 0, 0, 0);
                    }
                }
            } else if (strcmp(topic, RIGHT_DOOR_STATUS_TOPIC) == 0) {
                if (strcmp(data, "get") == 0) {
                    if (gpio_get_level(RIGHT_DOOR_SENSOR_GPIO)) {
                        esp_mqtt_client_publish(client, RIGHT_DOOR_STATUS_TOPIC, "status:open", 0, 0, 0);
                    } else {
                        esp_mqtt_client_publish(client, RIGHT_DOOR_STATUS_TOPIC, "status:closed", 0, 0, 0);
                    }
                }
            }

            free(topic);
            free(data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(TAG, "start the WIFI SSID:[%s] password:[%s]", CONFIG_WIFI_SSID, "******");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Waiting for wifi");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = CONFIG_MQTT_SERVER,
        .event_handle = mqtt_event_handler,
        // .user_context = (void *)your_context
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

void app_main()
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    /* Set the GPIO as a push/pull output */
    gpio_pad_select_gpio(LEFT_DOOR_GPIO);
    gpio_pad_select_gpio(RIGHT_DOOR_GPIO);
    gpio_set_direction(LEFT_DOOR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(RIGHT_DOOR_GPIO, GPIO_MODE_OUTPUT);
    /* Set these PINs high */
    gpio_set_level(LEFT_DOOR_GPIO, 1);
    gpio_set_level(RIGHT_DOOR_GPIO, 1);

    /* 
        Set the Sensors 
        Set the GPIO as a push/pull output 
    */
    gpio_set_direction(LEFT_DOOR_SENSOR_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(RIGHT_DOOR_SENSOR_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(LEFT_DOOR_SENSOR_GPIO);
    gpio_pullup_en(RIGHT_DOOR_SENSOR_GPIO);

    nvs_flash_init();
    wifi_init();
    mqtt_app_start();
}
