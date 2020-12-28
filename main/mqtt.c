//MQTT

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "core.h"

static const char *TAG = "MQTT";
esp_mqtt_client_handle_t mqttclient;
bool mqtt_connected = false;

static void log_error_if_nonzero(const char * message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void parseTopic(char* topic, char* data) {
    // ESP_LOGW(TAG, "parseTopic %s %s", topic, data);
    char * sDev = strtok(topic, "/");
    char * sSlave = strtok(NULL, "/");
    char * sOutput = strtok(NULL, "/");
    // ESP_LOGW(TAG, "parseTopic2 %s %s %s", sDev, sSlave, sOutput);
    if ((sDev == NULL) || (sSlave == NULL) || (sOutput == NULL)) {
        ESP_LOGE(TAG, "can't parse topic %s", topic);
        return;
    }
    if (getActionValue(data) == ACT_NOTHING) {
        ESP_LOGE(TAG, "can't parse data %s", data);
        return;    
    }
    uint8_t slaveid = atoi(sSlave);
    uint8_t outputid = atoi(sOutput);
    action_t action;
    action.slave_addr = slaveid;
    action.output = outputid;    
    action.action = getActionValue(data);    
    // ESP_LOGW(TAG, "slaveid %d, ouput %d, action %d", slaveid, outputid, action.action);
    SemaphoreHandle_t sem = getSemaphore();
    if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE) {
        addAction(action);
        xSemaphoreGive(sem);
    }    
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");            
            //msg_id = esp_mqtt_client_subscribe(client, "/main/#", 0);
            char stopic[100];
            strcpy(stopic, "/");
            strcat(stopic, getNetworkConfigValueString("hostname"));
            strcat(stopic, "/#");
            strcat(stopic, "\0");            
            msg_id = esp_mqtt_client_subscribe(client, stopic, 0);
            //msg_id = esp_mqtt_client_subscribe(client, "/main/#", 0);            
            mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            //msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
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
            // parse topic
            char topic[100];
            strncpy(topic, event->topic, event->topic_len);
            topic[event->topic_len] = 0;
            char data[20];
            strncpy(data, event->data, event->data_len);
            data[event->data_len] = 0;
            parseTopic(topic, data);
            // char *topic = malloc(event->topic_len+1);
            // strcpy(topic, event->topic);
            // topic[event->topic_len] = 0;
            // char *data = malloc(event->data_len+1);
            // strcpy(data, event->data);
            // data[event->data_len] = 0;
            // ESP_LOGI(TAG, "topic %s, data %s", topic, data);
            // parseTopic(topic, data);            
            // free(topic);
            // free(data);
            //parseTopic(event->topic, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

void mqttPublish(char* topic, char* data) {
    if (mqtt_connected)
        esp_mqtt_client_publish(mqttclient, topic, data, 0, 0, 0);    
}

static void mqtt_app_start(void)
{
    // ESP_LOGI(TAG, "mqtt uri %s", getNetworkConfigValueString("MQTTuri"));
    esp_mqtt_client_config_t mqtt_cfg = {
    //     .uri = "mqtt://mqtt:mqtt1@192.168.4.7:1883",        
        .uri = "mqtt://"
    };
    //esp_mqtt_client_config_t mqtt_cfg;    
    // char* mqtturi = getNetworkConfigValueString("MQTTuri");
    // char* uri = malloc(strlen(mqtturi)+1);
    // strcpy(uri, mqtturi);
    // uri[strlen(mqtturi)] = "\0";    
    //mqtt_cfg.uri = uri;
    //mqtt_cfg.uri = "mqtt://mqtt:mqtt1@192.168.4.7:1883";
    mqtt_cfg.uri = getNetworkConfigValueString("MQTTuri");
    
    //esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    mqttclient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqttclient, ESP_EVENT_ANY_ID, mqtt_event_handler, mqttclient);
    esp_mqtt_client_start(mqttclient);
}

void initMQTT() {
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    mqtt_app_start();
}
