//modbus.h

#define MB_READ_COILS           1
#define MB_READ_INPUTS          2
#define MB_READ_HOLDINGS        3
#define MB_READ_INPUTREGISTERS  4
#define MB_WRITE_SINGLECOIL     5
#define MB_WRITE_REGISTER       6    
#define MB_WRITE_MULTI_REGISTER 0x10
#define MB_WRITE_MULTI_COILS    0x0F    

esp_err_t setCoilQueue(uint8_t slaveid, uint8_t adr, uint8_t value);
esp_err_t setHoldingQueue(uint8_t slaveid, uint8_t adr, uint16_t value);
esp_err_t mbInit();
void setReadTimeOut(uint16_t val);
void processQueue();
esp_err_t executeModbusCommand(uint8_t slaveId, uint8_t command, uint8_t start, uint8_t qty, uint8_t **response);
void test();