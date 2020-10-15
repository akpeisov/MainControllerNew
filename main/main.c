#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "utils.h"
#include "storage.h"
#include "core.h"
#include "network.h"
#include "webServer.h"
#include "modbus.h"
#include "ftp.h"

static const char *TAG = "MAIN";

void wdtMemoryTask(void *pvParameter) {
    ESP_LOGI(TAG, "Create watchdog memory task");
    uint8_t cnt=0;
    uint32_t minMem = getServiceConfigValueInt("wdtmemsize");
    while(1)
    {   
        // every 1 second     
        if (esp_get_free_heap_size() < minMem) {
            ESP_LOGE(TAG, "HEAP memory WDT triggered. Actual free memory is %d. Restarting...", esp_get_free_heap_size());
            //writeLog("E", "HEAP watchdog triggered. Restarting system...");
            esp_restart();
        }
        if (isReboot()) {
            static uint8_t cntReboot = 0;
            if (cntReboot++ >= 3)
                esp_restart();
        }
    
        if (cnt++ >= 60) {            
            char *uptime = getUpTime();            
            if (uptime != NULL) {
                ESP_LOGW(TAG, "Free heap size is %d. Min free is %d. Uptime %s", 
                         esp_get_free_heap_size(), esp_get_minimum_free_heap_size(), uptime);
                free(uptime);
            }
            cnt=0;
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void modBusTask(void *pvParameter) {
    uint16_t pollingTime;
    ESP_LOGI(TAG, "Creating modBus task");
    SemaphoreHandle_t sem = getSemaphore();
    while(1)
    {     
        pollingTime = getServiceConfigValueInt("pollingTime");                         
        if (pollingTime == 0) {
            break;
        }
        if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE)
        {
            //ESP_LOGI(TAG, "modBus task semaphore working");
            // Здесь происходит защищенный доступ к ресурсу.               
            pollingNew();
            // Отдаем "жетон".
            xSemaphoreGive(sem);
        } else {
            ESP_LOGI(TAG, "modBus task semaphore is busy");
        }        
        vTaskDelay(pollingTime / portTICK_RATE_MS);        
    }
    //vSemaphoreDelete(sem);
    vTaskDelete(NULL);
}

void app_main(void)
{
    initLED();
    changeLEDStatus(LED_BOOTING);
    initStorage();
    loadConfig();
    if (initNetwork() == ESP_OK) {
        initWebServer();
    }
    mbInit();    
    //sem_busy = xSemaphoreCreateMutex();
    createSemaphore();
    xTaskCreate(&wdtMemoryTask, "wdtMemoryTask", 4096, NULL, 5, NULL);
    xTaskCreate(&modBusTask, "modBusTask", 4096, NULL, 5, NULL);        

    changeLEDStatus(LED_NORMAL);
    writeLog("I", "System started");

    initFTP();
}
