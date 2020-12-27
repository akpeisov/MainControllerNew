//dmx.c
#include "driver/uart.h"
#include "esp_err.h"
#include "dmx.h"
#include "core.h"
#include <string.h>
#include "esp_log.h"

#define DMX_PORT_NUM          1
#define BUF_SIZE            513
#define CONFIG_DMX_UART_TXD 32
#define CONFIG_DMX_UART_RXD 35
#define CONFIG_DMX_UART_RTS 33
#define DMX_PACKET_LEN      512

#define max(a, b)    (((a) > (b)) ? (a) : (b))
#define min(a, b)    (((a) < (b)) ? (a) : (b))

uint8_t *dmxData;
static const char *TAG = "DMX";

esp_err_t DMXSend() {    
    // gen BREAK and send packet
    //uart_write_bytes_with_break(uart_num, data, len); // break AFTER send :(
    /*
#define BREAKSPEED     83333
#define BREAKFORMAT    SERIAL_8N1
//Send break
  digitalWrite(sendPin, HIGH);
  Serial1.begin(BREAKSPEED, BREAKFORMAT);
  Serial1.write(0);
  Serial1.flush();
  delay(1);
  Serial1.end();
    */    
    //uart_write_bytes_with_break(DMX_PORT_NUM, "0", 1, 100);
    uart_set_baudrate(DMX_PORT_NUM, 90909);
    uart_write_bytes(DMX_PORT_NUM, "\0", 1);
    for (int i=0;i<2500;i++) {
        continue;
    }
    uart_set_baudrate(DMX_PORT_NUM, 250000);
    if (uart_write_bytes(DMX_PORT_NUM, (char*)dmxData, DMX_PACKET_LEN) != DMX_PACKET_LEN) {
        ESP_LOGE(TAG, "Can't write DMX data");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void dmx_task(void *pvParameters)
{
    SemaphoreHandle_t sem = getSemaphore();    
    while (1) {
        if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE) {
            processDMXDevices();
            DMXSend();    
            xSemaphoreGive(sem);
        }
        vTaskDelay(100 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

void DMXInit() {
    // init UART    
    const int uart_num = DMX_PORT_NUM; // 2 for modbus
    uart_config_t uart_config = {
        .baud_rate = 250000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_APB,
    };
    
    // Set UART log level
    ESP_LOGI(TAG, "Starting RS485 DMX");
    // Install UART driver (we don't need an event queue here)
    // In this example we don't even use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(uart_num, BUF_SIZE, BUF_SIZE, 0, NULL, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_LOGI(TAG, "UART set pins, mode and install driver.");
    // Set UART pins as per KConfig settings
    ESP_ERROR_CHECK(uart_set_pin(uart_num, CONFIG_DMX_UART_TXD, CONFIG_DMX_UART_RXD, CONFIG_DMX_UART_RTS, (UART_PIN_NO_CHANGE)));
    // Set RS485 half duplex mode
    ESP_ERROR_CHECK(uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX));
    // Set read timeout of UART TOUT feature
    //ESP_ERROR_CHECK(uart_set_rx_timeout(uart_num, ECHO_READ_TOUT));
    dmxData = malloc(DMX_PACKET_LEN);
    bzero(dmxData, DMX_PACKET_LEN);

    xTaskCreate(dmx_task, "DMX", 4096, NULL, 5, NULL);
}

void hsv2rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    h %= 360; // h -> [0,360]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

void rgb2hsv(uint8_t rr, uint8_t gg, uint8_t bb, uint16_t *h, uint8_t *s, uint8_t *v) {
    float vMax;
    float vMin;
    float r = rr / 255.0;
    float g = gg / 255.0;
    float b = bb / 255.0;
    vMax = max(max(r,g),b);
    vMin = min(min(r,g),b);
    if ((vMax == r) && (g >= b)) {
        *h = 60*((g-b)/(vMax-vMin));
    } else if ((vMax == r) && (g < b)) {
        *h = 60*((g-b)/(vMax-vMin))+360;
    } else if (vMax == g) {
        *h = 60*((b-r)/(vMax-vMin))+120;
    } else if (vMax == b) {
        *h = 60*((r-g)/(vMax-vMin))+240;
    }
    if (vMax == 0)
        *s = 0;
    else
        *s = (1-vMin/vMax)*100;
    *v = vMax*100;
}

void setDMXData(uint8_t address, uint8_t value) {
    dmxData[address] = value;    
}

// void DMXProcess() {
//     // таск для DMX
//     // будет выпуливать весь доступный стек (только до последнего объявленного адреса) постоянно.
//     // Надо использовать мютекс для доступа к данным?
//     // формирование сообщения
//     DMXSend();    
// }