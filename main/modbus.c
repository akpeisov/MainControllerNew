//modbus.c
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "modbus.h"

#define CONFIG_MB_UART_TXD 4
#define CONFIG_MB_UART_RXD 16
#define CONFIG_MB_UART_RTS 5
#define MB_PORT_NUM    2
#define MB_DEV_SPEED   115200
#define MAX_REQUESTS 10

#define BUF_SIZE              (127)
#define PACKET_READ_TICS      (20 / portTICK_RATE_MS)
#define ECHO_TASK_STACK_SIZE  (2048)
#define ECHO_READ_TOUT        (3)
#define POLY                  0xA001

static const char* TAG = "MODBUS";

static uint8_t queueWriteIndex;
uint16_t readTimeOut = 20;

typedef enum {
    MB_OK = 0,
    MB_SEND_FAILED,
    MB_RECEIVE_ERROR,
    MB_RECEIVE_TIMEOUT,
    MB_CHECKSUM_ERROR,
    MB_ERROR_CODE
} send_packet_res_t;

typedef struct {
    uint8_t slave_addr;             /*!< Modbus slave address */
    uint8_t command;                /*!< Modbus command to send */
    uint16_t reg_start;             /*!< Modbus start register */
    uint16_t reg_size;              /*!< Modbus number of registers */    
    uint16_t values[10];            // IO registers 
    uint8_t response[10];           // IO raw values
    uint8_t result;                 // ref to send_packet_res_t
} mb_param_t;

mb_param_t *mbQueue[MAX_REQUESTS]; // array of pointers
bool mbQueueExist[MAX_REQUESTS];

void setReadTimeOut(uint16_t val) {
    readTimeOut = val;
}

void initQueue() {
    queueWriteIndex = 0;
    
    for (uint8_t i=0; i<MAX_REQUESTS; i++) {
        mbQueue[i] = NULL;
        mbQueueExist[i] = false;
    }
}

esp_err_t allocateQueue(mb_param_t** param) {
    //    
    // if (mbQueue[queueWriteIndex] != NULL) {
    //     // already allocated. free?
    //     free(mbQueue[queueWriteIndex]);
    // }
    mbQueue[queueWriteIndex] = malloc(sizeof(mb_param_t));
    if (mbQueue[queueWriteIndex] == NULL) {
        ESP_LOGE(TAG, "Can't alloc mbQueue %d", queueWriteIndex);
        return ESP_FAIL;
    }
    *param = mbQueue[queueWriteIndex];
    mbQueueExist[queueWriteIndex] = true;
    if (++queueWriteIndex >= MAX_REQUESTS)
        queueWriteIndex = 0;
    return ESP_OK;
}

esp_err_t setCoilQueue(uint8_t slaveid, uint8_t adr, uint8_t value) {
    uint16_t val = 0xFF00;    
    if (value == 0)
        val = 0;
    mb_param_t *mbRequest;
    if (allocateQueue(&mbRequest) != ESP_OK) {
        return ESP_FAIL;
    }
    mbRequest->slave_addr = slaveid;
    mbRequest->command = MB_WRITE_SINGLECOIL;
    mbRequest->reg_start = adr;
    mbRequest->reg_size = 1;
    mbRequest->values[0] = val;
    return ESP_OK;
}

esp_err_t setHoldingQueue(uint8_t slaveid, uint8_t adr, uint16_t value) {    
    mb_param_t *mbRequest;
    if (allocateQueue(&mbRequest) != ESP_OK) {
        return ESP_FAIL;
    }
    mbRequest->slave_addr = slaveid;
    mbRequest->command = MB_WRITE_REGISTER;
    mbRequest->reg_start = adr;
    mbRequest->reg_size = 1;
    mbRequest->values[0] = value;
    // ESP_LOGI(TAG, "Slaveid %d, adr %d, value %d", slaveid, adr, value);
    // ESP_LOGI(TAG, "Slaveid %d, adr %d, value %d", mbRequest->slave_addr, mbRequest->reg_start, mbRequest->values[0]);
    return ESP_OK;
}

esp_err_t mbInit() {    
    const int uart_num = MB_PORT_NUM;
    uart_config_t uart_config = {
        .baud_rate = MB_DEV_SPEED,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_APB,
    };
    
    // Set UART log level
    ESP_LOGI(TAG, "Start RS485 application test and configure UART.");
    // Install UART driver (we don't need an event queue here)
    // In this example we don't even use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(uart_num, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_LOGI(TAG, "UART set pins, mode and install driver.");
    // Set UART pins as per KConfig settings
    ESP_ERROR_CHECK(uart_set_pin(uart_num, CONFIG_MB_UART_TXD, CONFIG_MB_UART_RXD, CONFIG_MB_UART_RTS, (UART_PIN_NO_CHANGE)));
    // Set RS485 half duplex mode
    ESP_ERROR_CHECK(uart_set_mode(uart_num, UART_MODE_RS485_HALF_DUPLEX));
    // Set read timeout of UART TOUT feature
    ESP_ERROR_CHECK(uart_set_rx_timeout(uart_num, ECHO_READ_TOUT));
    // Allocate buffers for UART
    //uint8_t* data = (uint8_t*) malloc(BUF_SIZE);
    ESP_LOGI(TAG, "UART start recieve loop.\r\n");
    //echo_send(uart_num, "Start RS485 UART test.\r\n", 24);
    initQueue();    
    return ESP_OK;
}

uint16_t calcModbusCRC(uint8_t *data, uint8_t len) {
    uint16_t crc = 0xFFFF;
    uint8_t i = 0;
    while (len--)
    {
        //crc ^= *data++;
        crc ^= data[i++];
        for (uint8_t i = 8; i != 0; i--)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ POLY;
            else
                crc >>= 1;
        }        
    }
    return (crc);
}

bool checkCRC(uint8_t *data, uint8_t len) {
    uint16_t crc = calcModbusCRC(data, len-2);
    return (data[len-2] == (uint8_t)crc && data[len-1] == (uint8_t)(crc>>8));
}

send_packet_res_t sendPacket(uint8_t *txData, uint8_t txLen_, uint8_t **rxData, uint8_t *rxLen) {
    // send data    
    // add crc
    uint8_t txLen = txLen_;
    // ESP_LOGI(TAG, "txLen %d", txLen);
    // ESP_LOG_BUFFER_HEXDUMP("txData", txData, txLen, CONFIG_LOG_DEFAULT_LEVEL);
    uint16_t crc = calcModbusCRC(txData, txLen);    
    txData[txLen++] = crc;
    txData[txLen++] = crc >> 8;
    // ESP_LOG_BUFFER_HEXDUMP("txData", txData, txLen, CONFIG_LOG_DEFAULT_LEVEL);
    
    if (uart_write_bytes(MB_PORT_NUM, (char*)txData, txLen) != txLen) {
        return MB_SEND_FAILED;
    }
    // receive data        
    uint8_t* dtmp = (uint8_t*) malloc(BUF_SIZE); 
    bzero(dtmp, BUF_SIZE);
    *rxLen = uart_read_bytes(MB_PORT_NUM, dtmp, BUF_SIZE, readTimeOut / portTICK_RATE_MS);    
    
/*
    dtmp[0] = 0x01;
    dtmp[1] = 0x06;
    dtmp[2] = 0x00;
    dtmp[3] = 0x00;
    dtmp[4] = 0x00;
    dtmp[5] = 0xCC;
    dtmp[6] = 0x89;
    dtmp[7] = 0x9F; 
    *rxLen = 8;   
*/
    *rxData = dtmp;

    //*rxLen = uart_read_bytes(MB_PORT_NUM, rxData, BUF_SIZE, readTimeOut / portTICK_RATE_MS);    
    // uart_flush(MB_PORT_NUM);
    // check crc
    if (*rxLen == 0) {
        return MB_RECEIVE_TIMEOUT;
    }
    if (*rxLen < 4) {
        return MB_RECEIVE_ERROR;
    }
    if (!checkCRC(*rxData, *rxLen)) {    
        return MB_CHECKSUM_ERROR;    
    }
    *rxLen=*rxLen-2;
    return MB_OK;
}

esp_err_t my_master_send_request(mb_param_t* request) {    
    uint8_t txLen = 4 + request->reg_size*2; // addres + cmd + start + start + values + ... + crc
    if (request->command == MB_WRITE_MULTI_REGISTER) {
        txLen += 1+2;
    }
    //uint8_t bufSize = 5;
    uint8_t *txData = (uint8_t*)malloc(txLen+2); //+2 for CRC
    // heap_caps_check_integrity_all(true);
    if (txData == NULL) {
        return ESP_FAIL;
    }
    txData[0] = request->slave_addr;
    txData[1] = request->command;
    txData[2] = request->reg_start >> 8;
    txData[3] = request->reg_start;
    if ((request->command == MB_WRITE_SINGLECOIL) || 
        (request->command == MB_WRITE_REGISTER)) {        
        txData[4] = request->values[0] >> 8;    
        txData[5] = (uint8_t)request->values[0];
    } else {
        txData[4] = request->reg_size >> 8;
        txData[5] = request->reg_size;   
    }

    if (request->command == MB_WRITE_MULTI_REGISTER) {
        txData[6] = request->reg_size * 2;
        for (uint8_t i=0;i<request->reg_size;i++) {
            txData[7+i] = request->values[0] >> 8;
            txData[7+i+1] = (uint8_t)request->values[0];
        }
        txLen = 6 + request->reg_size*2 + 1;
    } else if (request->command == MB_WRITE_MULTI_COILS) {
        // not inmplemented yet        
        //bufSize += request->reg_size*2;
        //txLen = 6; // HZ
        free(txData);        
        return ESP_FAIL;
    }
        
    uint8_t *rxData;// = malloc(BUF_SIZE); 
    heap_caps_check_integrity_all(true);  
    /*
    if (rxData == NULL) {
        ESP_LOGE(TAG, "Can't allocate buffer for rxData");
        free(txData);
        return ESP_FAIL;
    }
    */
    // memset(rxData, 0, BUF_SIZE);
    uint8_t rxLen = 0;
    send_packet_res_t err = sendPacket(txData, txLen, &rxData, &rxLen);
    //send_packet_res_t err = MB_RECEIVE_TIMEOUT;

//
    // rxData = (uint8_t*)malloc(BUF_SIZE);
    // rxData[0] = 0x01;
    // rxData[1] = 0x06;
    // rxData[2] = 0x00;
    // rxData[3] = 0x00;
    // rxData[4] = 0x00;
    // rxData[5] = 0xCC;
    // rxData[6] = 0x89;
    // rxData[7] = 0x9F; 
    // rxLen = 8;   
    // send_packet_res_t err = MB_OK;
//

    if (err == MB_OK) {
        // everything is ok, return values    
        // check for error
        if (rxData[1] >= 0x80) {
            ESP_LOGE(TAG, "Received error #%d", rxData[2]);
            err = MB_ERROR_CODE;
        }
        uint8_t dataLen = rxData[2]; // in bytes
        if (dataLen > rxLen-3) { // address, command, length, crc
            ESP_LOGE(TAG, "Data length mismatch long! DataLen %d rxLen %d", dataLen, rxLen);
            ESP_LOG_BUFFER_HEXDUMP("rxData", rxData, rxLen, CONFIG_LOG_DEFAULT_LEVEL);
            err = MB_RECEIVE_ERROR;
        } else {
            if ((request->command == MB_READ_COILS) || 
                (request->command == MB_READ_INPUTS) ||
                (request->command == MB_READ_HOLDINGS) || 
                (request->command == MB_READ_INPUTREGISTERS)) {                            
                for (uint8_t i=0;i<dataLen/2;i++) {
                    request->values[i] = rxData[3+i];
                    request->values[i]<<=8;
                    request->values[i] |= rxData[3+i+1];
                }
                for (uint8_t i=0;i<dataLen;i++) {
                    request->response[i] = rxData[3+i];
                }
            }
            // other commands for set data            
        }        
    } else if (err == MB_SEND_FAILED) {        
        ESP_LOGE(TAG, "Error while sending request");                
        ESP_LOG_BUFFER_HEXDUMP("txData", txData, txLen, CONFIG_LOG_DEFAULT_LEVEL);
    } else if (err != MB_OK) {
        ESP_LOGE(TAG, "Error while getting response. Errcode %d", err);
        ESP_LOG_BUFFER_HEXDUMP("txData", txData, txLen, CONFIG_LOG_DEFAULT_LEVEL);
        ESP_LOG_BUFFER_HEXDUMP("rxData", rxData, rxLen, CONFIG_LOG_DEFAULT_LEVEL);
    }
    heap_caps_check_integrity_all(true);       
    if (txData != NULL)
        free(txData);
    if (rxData != NULL)
        free(rxData);

    if (err != MB_OK)
        return ESP_FAIL;
    return ESP_OK;   
}

// mb_param_t* getQueue() { 
//     // вернет указатель на очередную запись запроса
//     for (uint8_t i=0; i<MAX_REQUESTS; i++) {
//         if (mbQueue[i] != NULL) {
//             ESP_LOGI(TAG, "Queue num %d", i);
//             return mbQueue[i];
//         }        
//     }
//     return NULL;
// }

bool getQueue(mb_param_t** param, uint8_t *indx) { 
    for (uint8_t i=0; i<MAX_REQUESTS; i++) {
        if (mbQueueExist[i]) {
            ESP_LOGI(TAG, "Queue num %d", i);
            *param = mbQueue[i];
            *indx = i;
            return true;
        }        
    }
    return false; // очередь пуста
}

void delQueue(uint8_t indx) {
    if (mbQueue[indx] != NULL)
        free(mbQueue[indx]);
    mbQueueExist[indx] = false;
}

void processQueue() {
    // process queue
    // for set coil or set holdings
    esp_err_t res;
    mb_param_t *req;
    uint8_t queueIndx;
    while (getQueue(&req, &queueIndx)) {                
        ESP_LOGI(TAG, "Queue index is %d", queueIndx);
        res = my_master_send_request(req);
        //res = ESP_OK;
        if (res == ESP_OK) {
            ESP_LOGI(TAG, "processQueue. Command %d executed succesfull on slave %d. start %d value %d", 
                              req->command, req->slave_addr, req->reg_start, req->values[0]);
        } else {
            ESP_LOGE(TAG, "processQueue. Can't execute queue command %d on slave %d", req->command, req->slave_addr);
        }           
        delQueue(queueIndx);
    }
}

esp_err_t executeModbusCommand(uint8_t slaveId, uint8_t command, uint8_t start, uint8_t qty, uint8_t **response) {
    // выполнить модбас команду, вернется или хорошо или плохо, код ошибки в параметре
    mb_param_t *request = malloc(sizeof(mb_param_t));
    if (request == NULL) {
        ESP_LOGE(TAG, "Can't malloc request");
        return ESP_FAIL;
    }
    *response = malloc(qty*2); // two bytes for 1 register
    if (*response == NULL) {
        ESP_LOGE(TAG, "Can't malloc response");
        return ESP_FAIL;
    }
    request->slave_addr = slaveId;
    request->command = command;
    request->reg_start = start;
    request->reg_size = qty;    
    if (my_master_send_request(request) != ESP_OK) {
        free(*response);
        free(request);
        return ESP_FAIL;
    }
    //memcpy(*response, request->values, qty*2);
    memcpy(*response, request->response, qty*2);
    free(request);
    return ESP_OK;
}

// void test() {
//     uint8_t *rxData = malloc(BUF_SIZE);
//     heap_caps_check_integrity_all(true);
//     memset(rxData, 0, BUF_SIZE);
//     rxData[10] = 0xAA;
//     free(rxData);
//     heap_caps_check_integrity_all(true);
// }

/*
void processQueue() {
    // process queue
    // for set coil or set holdings
    esp_err_t res;
    mb_param_t *req;    
    while (1) {
        req = getQueue();
        if (req == NULL) {
            break;
        }
        res = my_master_send_request(req);
        if (res == ESP_OK) {
            ESP_LOGI(TAG, "processQueue. Command %d executed succesfull on slave %d. start %d value %d", 
                              req->command, req->slave_addr, req->reg_start, req->values[0]);
        } else {
            ESP_LOGE(TAG, "processQueue. Can't execute queue command %d on slave %d", req->command, req->slave_addr);
        }   
        free(req);        
    }
}

*/
/*
    AA - hi byte address, aa - lo byte address
    CC - hi byte count, cc - lo byte address
    CR - crc
    01 read coils
    02 01 AA aa CC cc CR CR 
    02 01 BC ab cd CR CR   BC - byte count, ab cd - data
    02 read discrete inputs
    02 02 AA aa CC cc CR CR
    02 02 BC ab cd CR CR   BC - byte count, ab cd - data
    03 read holding
    02 03 AA aa CC cc CR CR
    02 03 BC ab cd CR CR   BC - byte count, ab cd - data
    04 read input registers
    02 04 AA aa CC cc CR CR
    02 04 BC ab cd CR CR   BC - byte count, ab cd - data
    05 write single coil
    02 05 AA aa 00 FF CR CR  -- 00 FF - set 00 00 clear
    02 05 AA aa 00 FF CR CR  answer the same
    06 write register
    02 06 AA aa VV VV CR CR
    02 06 AA aa VV VV CR CR  answer the same
    0F write coils hz kak tut
    02 0F AA aa CC cc MM MM CR CR  MM - bit mask set
    02 0F AA aa CC cc CR CR
    10 write registers
    02 10 00 0b 00 02 04 01 02 03 04 CC CC
    02 10 00 0b 00 02 CC CC

    //                                    adr cmd len dta crc
            // coils    [RTU]>Rx > 15:48:36:440 - 01  01  01  00  51  88  
            // inputs   [RTU]>Rx > 15:49:05:395 - 01  02  01  00  A1  88  
            // holdings [RTU]>Rx > 15:42:32:439 - 01  03  06  53  32  41  00  00  42  80  DF  
            // inp regs [RTU]>Rx > 15:49:35:710 - 01  04  06  00  00  00  00  00  00  60  93  
    */