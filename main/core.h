//core.h
#include "webServer.h"
#include "freertos/semphr.h"

//SemaphoreHandle_t sem_busy;

esp_err_t loadConfig();
uint16_t getServiceConfigValueInt(const char* name);
uint16_t getNetworkConfigValueInt(const char* name);
bool getNetworkConfigValueBool(const char* name);
char *getNetworkConfigValueString(const char* name);
esp_err_t uiRouter(httpd_req_t *req);
bool isReboot();
void pollingNew();

SemaphoreHandle_t getSemaphore();
void createSemaphore();
void initCore();