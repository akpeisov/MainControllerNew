//core.h
#include "webServer.h"
#include "freertos/semphr.h"

//SemaphoreHandle_t sem_busy;
typedef enum {
    LED_BOOTING, // booting
    LED_NORMAL,  // normal work
    LED_ERROR,   // some error    
    LED_OTA      // OTA update
} led_status_t;

typedef struct {
    uint8_t slave_addr; // адрес устройства назначения
    uint8_t output;     // номер выхода           
    uint8_t action;     // действие action_type_t
} action_t;

typedef enum {
    ACT_NOTHING, // nothing 
    ACT_ON,      // switch on 
    ACT_OFF,     // switch off
    ACT_TOGGLE   // toggle     
} action_type_t;

void initLED();
void changeLEDStatus(uint8_t status);
esp_err_t loadConfig();
uint16_t getServiceConfigValueInt(const char* name);
uint16_t getNetworkConfigValueInt(const char* name);
bool getNetworkConfigValueBool(const char* name);
char *getNetworkConfigValueString(const char* name);
esp_err_t uiRouter(httpd_req_t *req);
bool isReboot();
void pollingNew();

void processDMXDevices();
SemaphoreHandle_t getSemaphore();
void createSemaphore();
void phyPower(bool on_off);
uint8_t getActionValue(char * str);
void addAction(action_t pAction);