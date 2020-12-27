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