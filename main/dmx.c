//dmx.c
#include "driver/uart.h"
#include "esp_err.h"
#include "dmx.h"

#define MB_PORT_NUM          1
#define BUF_SIZE            (127)
#define CONFIG_DMX_UART_TXD 32
#define CONFIG_DMX_UART_RXD 35
#define CONFIG_DMX_UART_RTS 33
#define DMX_PACKET_LEN      512
uint8_t *dmxData;

void DMXInit() {
    // init UART    
    const int uart_num = MB_PORT_NUM; // 2 for modbus
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
    ESP_ERROR_CHECK(uart_driver_install(uart_num, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
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
}

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
    uart_write_bytes_with_break(uart_num, "0", 1);
    if (uart_write_bytes(MB_PORT_NUM, (char*)dmxData, DMX_PACKET_LEN) != DMX_PACKET_LEN) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

RGB_t HSVtoRGB(HSV_t hsv) {
    uint16_t H = hsv.h;
    uint8_t S = hsv.s;
    uint8_t V = hsv.v;
    float s = S/100.0;
    float v = V/100.0;
    float C = s*v;
    float X = C*(1-abs(fmod(H/60.0, 2)-1));
    float m = v-C;
    uint8_t r,g,b;
    if(H >= 0 && H < 60){
        r = C,g = X,b = 0;
    }
    else if(H >= 60 && H < 120){
        r = X,g = C,b = 0;
    }
    else if(H >= 120 && H < 180){
        r = 0,g = C,b = X;
    }
    else if(H >= 180 && H < 240){
        r = 0,g = X,b = C;
    }
    else if(H >= 240 && H < 300){
        r = X,g = 0,b = C;
    }
    else{
        r = C,g = 0,b = X;
    }
    r = (r+m)*255.0;
    g = (g+m)*255.0;
    b = (b+m)*255.0;
    return RGB_t(r,g,b);
}

void hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b) {
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


void setDMXData(uint8_t address, uint8_t value) {
    dmxData[address] = value;    
}

void DMXProcess() {
    // таск для DMX
    // будет выпуливать весь доступный стек (только до последнего объявленного адреса) постоянно.
    // Надо использовать мютекс для доступа к данным?
    // формирование сообщения
    DMXSend();    
}