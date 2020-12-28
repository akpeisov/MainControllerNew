//core.c
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "storage.h"
#include "webServer.h"
#include "modbus.h"
#include "utils.h"
#include "freertos/semphr.h"
#include "ota.h"
#include "core.h"
#include "driver/gpio.h"
#include "dmx.h"
#include "mqtt.h"

static const char *TAG = "CORE";
static cJSON *networkConfig;
static cJSON *serviceConfig;
static cJSON *devices;
static cJSON *DMXdevices;
static cJSON *temperaturesData;

//network config defaults
#define DEF_IP          "192.168.99.9"
#define DEF_IPW         "192.168.99.10"
#define DEF_MASK        "255.255.255.0"
#define DEF_GW          "192.168.99.98"
#define DEF_DNS         "192.168.99.98"
#define DEF_WIFI_SSID   "DevCtrl"
#define DEF_WIFI_PASS   "60380003"
#define DEF_DHCP_EN     1
//service config defaults
#define DEF_NAME        "Device-0"
#define DEF_USERNAME    "admin"
#define DEF_PASSWORD    "admin"
#define MAX_DEVICES     10
#define MAX_ACTIONS     10
#define MSG_BUFFER      100

#define STATUS_LED      12

#define  setbit(var, bit)    ((var) |= (1 << (bit)))
#define  clrbit(var, bit)    ((var) &= ~(1 << (bit)))

uint8_t ledStatus = LED_BOOTING;

typedef struct {
    uint8_t slave_addr; // на каком устройстве произошло событие
    uint8_t input;      // на каком входе         
    uint8_t event;      // какое событие event_type_t
} event_t;

typedef enum {
    EV_NOTHING, // nothing happends
    EV_ON,  // event on 
    EV_OFF, // event off
    EV_PRESSED, // event pressed (for button)
    EV_LONGPRESSED, // event long pressed (for button)
    EV_DBLPRESSED // event double pressed (for button)
} event_type_t;

bool reboot = false;
static uint8_t actions_qty = 0;
SemaphoreHandle_t sem_busy;
uint8_t pollingList[MAX_DEVICES];
static action_t* actions[MAX_ACTIONS];

esp_err_t loadDevices() {
    char * buffer = NULL;
    if (loadTextFile("/config/devices.json", &buffer) == ESP_OK) {
        cJSON *parent = cJSON_Parse(buffer);
        if (cJSON_IsArray(parent)) {            
            cJSON_Delete(devices);
            devices = parent;                    
        } else {
            ESP_LOGI(TAG, "devices is not a json, creating empty array");       
            devices = cJSON_CreateArray();
        }
    } else {
        ESP_LOGI(TAG, "can't read devices config. creating empty array");
        cJSON_Delete(devices);
        devices = cJSON_CreateArray();        
    }
    if (buffer != NULL)
        free(buffer);

    //ESP_LOGI(TAG, "config %s", cJSON_Print(devices));
    return ESP_OK;
}

esp_err_t saveDevices() {
    char * dev = cJSON_Print(devices);
    esp_err_t err = saveTextFile("/config/devices.json", dev);
    free(dev);
    return err;
}

esp_err_t saveDMXDevices() {
    char * dev = cJSON_Print(DMXdevices);
    esp_err_t err = saveTextFile("/config/DMXdevices.json", dev);
    free(dev);
    return err;
}

esp_err_t loadDMXDevices() {
    char * buffer = NULL;
    bool ok = false;
    if (loadTextFile("/config/DMXdevices.json", &buffer) == ESP_OK) {
        cJSON *parent = cJSON_Parse(buffer);
        if (cJSON_IsArray(parent)) {
            DMXdevices = parent;            
            ok = true;
        }
    }
    if (!ok) {     
        ESP_LOGI(TAG, "can't read DMXdevices config. creating null config");       
        cJSON_Delete(DMXdevices);
        DMXdevices = cJSON_CreateArray();        
        saveDMXDevices();
    }
    if (buffer != NULL)
        free(buffer);
    return ESP_OK;
}

esp_err_t saveTemperatures() {
    char * dev = cJSON_Print(temperaturesData);
    esp_err_t err = saveTextFile("/config/temperatures.json", dev);
    free(dev);
    return err;
}

esp_err_t loadTemperatures() {
    char * buffer = NULL;
    bool ok = false;
    if (loadTextFile("/config/temperatures.json", &buffer) == ESP_OK) {
        cJSON *parent = cJSON_Parse(buffer);
        if (cJSON_IsArray(parent)) {
            temperaturesData = parent;
            ok = true;
        }
    }
    if (!ok) {
        ESP_LOGI(TAG, "can't read temperaturesData config. creating null config");       
        cJSON_Delete(temperaturesData);
        temperaturesData = cJSON_CreateArray();        
        saveTemperatures();
    }
    if (buffer != NULL)
        free(buffer);
    return ESP_OK;
}

esp_err_t createNetworkConfig() {
    networkConfig = cJSON_CreateObject();
    cJSON_AddItemToObject(networkConfig, "networkmode", cJSON_CreateNumber(0));
    cJSON_AddItemToObject(networkConfig, "ethdhcp", cJSON_CreateBool(DEF_DHCP_EN));
    cJSON_AddItemToObject(networkConfig, "ethip", cJSON_CreateString(DEF_IP));
    cJSON_AddItemToObject(networkConfig, "ethnetmask", cJSON_CreateString(DEF_MASK));
    cJSON_AddItemToObject(networkConfig, "ethgateway", cJSON_CreateString(DEF_GW));
    
    cJSON_AddItemToObject(networkConfig, "wifi_ssid", cJSON_CreateString(DEF_WIFI_SSID));
    cJSON_AddItemToObject(networkConfig, "wifi_pass", cJSON_CreateString(DEF_WIFI_PASS));
    cJSON_AddItemToObject(networkConfig, "wifidhcp", cJSON_CreateBool(DEF_DHCP_EN));
    cJSON_AddItemToObject(networkConfig, "wifiip", cJSON_CreateString(DEF_IPW));
    cJSON_AddItemToObject(networkConfig, "wifinetmask", cJSON_CreateString(DEF_MASK));
    cJSON_AddItemToObject(networkConfig, "wifigateway", cJSON_CreateString(DEF_GW));

    cJSON_AddItemToObject(networkConfig, "dns", cJSON_CreateString(DEF_DNS));
    cJSON_AddItemToObject(networkConfig, "hostname", cJSON_CreateString(DEF_NAME));
    cJSON_AddItemToObject(networkConfig, "ntpserver", cJSON_CreateString("pool.ntp.org"));
    cJSON_AddItemToObject(networkConfig, "ntpTZ", cJSON_CreateString("Asia/Almaty"));
    cJSON_AddItemToObject(networkConfig, "configured", cJSON_CreateFalse());
    cJSON_AddItemToObject(networkConfig, "otaurl", cJSON_CreateString("https://192.168.99.6:8443/MainControllerNew.bin"));
    
    return ESP_OK;
}

esp_err_t saveNetworkConfig() {
    char *net = cJSON_Print(networkConfig);
    esp_err_t err = saveTextFile("/config/networkconfig.json", net);
    free(net);
    return err;
}

esp_err_t loadNetworkConfig() {
    char * buffer;
    if (loadTextFile("/config/networkconfig.json", &buffer) == ESP_OK) {
        cJSON *parent = cJSON_Parse(buffer);
        if(!cJSON_IsObject(parent) && !cJSON_IsArray(parent))
        {
            free(buffer);
            return ESP_FAIL;
        }
        cJSON_Delete(networkConfig);
        networkConfig = parent;        
    } else {
        ESP_LOGI(TAG, "can't read networkConfig. creating default config");
        cJSON_Delete(networkConfig);
        if (createNetworkConfig() == ESP_OK)
            saveNetworkConfig();        
    }
    free(buffer);
    return ESP_OK;
}

esp_err_t createServiceConfig() {
    serviceConfig = cJSON_CreateObject();
    cJSON_AddItemToObject(serviceConfig, "pollingTime", cJSON_CreateNumber(5000));
    cJSON_AddItemToObject(serviceConfig, "pollingTimeout", cJSON_CreateNumber(100));
    cJSON_AddItemToObject(serviceConfig, "pollingRetries", cJSON_CreateNumber(100));
    cJSON_AddItemToObject(serviceConfig, "waitingRetries", cJSON_CreateNumber(100));    
    cJSON_AddItemToObject(serviceConfig, "savePeriod", cJSON_CreateNumber(60));
    cJSON_AddItemToObject(serviceConfig, "httpEnable", cJSON_CreateTrue());
    cJSON_AddItemToObject(serviceConfig, "httpsEnable", cJSON_CreateTrue());
    cJSON_AddItemToObject(serviceConfig, "authEnable", cJSON_CreateFalse());
    cJSON_AddItemToObject(serviceConfig, "authUser", cJSON_CreateString(DEF_USERNAME));
    cJSON_AddItemToObject(serviceConfig, "authPass", cJSON_CreateString(DEF_PASSWORD));
    cJSON_AddItemToObject(serviceConfig, "wdteth", cJSON_CreateTrue());
    cJSON_AddItemToObject(serviceConfig, "wdtmemsize", cJSON_CreateNumber(10000));
    cJSON_AddItemToObject(serviceConfig, "wdtmem", cJSON_CreateFalse());
    cJSON_AddItemToObject(serviceConfig, "actionslaveproc", cJSON_CreateTrue());    
    return ESP_OK;
}

void stopPolling() {    
    cJSON_ReplaceItemInObject(serviceConfig, "pollingTime", cJSON_CreateNumber(0));                      
}

esp_err_t saveServiceConfig() {
    char *svc = cJSON_Print(serviceConfig);
    esp_err_t err = saveTextFile("/config/serviceconfig.json", svc);
    free(svc);
    return err;
}

esp_err_t loadServiceConfig() {
    char * buffer;
    if (loadTextFile("/config/serviceconfig.json", &buffer) == ESP_OK) {
        cJSON *parent = cJSON_Parse(buffer);
        if(!cJSON_IsObject(parent) && !cJSON_IsArray(parent))
        {
            free(buffer);
            return ESP_FAIL;
        }
        cJSON_Delete(serviceConfig);
        serviceConfig = parent;        
        free(buffer);
    } else {
        ESP_LOGI(TAG, "can't read serviceconfig. creating default config");
        cJSON_Delete(serviceConfig);
        if (createServiceConfig() == ESP_OK)
            saveServiceConfig();        
    }    
    // setting uart receive timeout
    //setReadTimeOut(getReadTimeOut()); //readtimeout new // TODO : uncomment for modbus
    return ESP_OK;
}

esp_err_t loadConfig() {
    if ((loadNetworkConfig() == ESP_OK) && 
        (loadServiceConfig() == ESP_OK) && 
        (loadDevices() == ESP_OK) &&
        (loadDMXDevices() == ESP_OK) &&
        (loadTemperatures() == ESP_OK)) {
        return ESP_OK;
    }    
    return ESP_FAIL;
}

uint16_t getServiceConfigValueInt(const char* name) {
    if (!cJSON_IsNumber(cJSON_GetObjectItem(serviceConfig, name)))    
        return 0;
    return cJSON_GetObjectItem(serviceConfig, name)->valueint;
}

uint16_t getNetworkConfigValueInt(const char* name) {
    if (!cJSON_IsNumber(cJSON_GetObjectItem(networkConfig, name)))    
        return 0;
    return cJSON_GetObjectItem(networkConfig, name)->valueint;
}

bool getNetworkConfigValueBool(const char* name) {
    return cJSON_IsTrue(cJSON_GetObjectItem(networkConfig, name));
}

char *getNetworkConfigValueString(const char* name) {
    if (!cJSON_IsString(cJSON_GetObjectItem(networkConfig, name)))    
        return NULL;
    return cJSON_GetObjectItem(networkConfig, name)->valuestring;
}

bool getServiceConfigValueBool(const char* name) {
    return cJSON_IsTrue(cJSON_GetObjectItem(serviceConfig, name));
}

bool isReboot() {
    return reboot;
}

// void setErrorText(char **response, const char *text) {
//     // если надо аллокейтить внутри функции, то надо на вход передать адрес &dst, а внутри через *работать, в хидере **
//     *response = (char*)malloc(strlen(text)+1);
//     strcpy(*response, text);    
//     ESP_LOGE(TAG, "%s", text);
// }

void setErrorText(char **response, const char *text, ...) {
    char dest[1024]; // maximum lenght
    va_list argptr;
    va_start(argptr, text);
    vsprintf(dest, text, argptr);
    va_end(argptr);
    *response = (char*)malloc(strlen(dest)+1);
    strcpy(*response, dest);
    ESP_LOGE(TAG, "%s", dest);
}

// void setText(char **response, const char *text) {
//     *response = (char*)malloc(strlen(text)+1);
//     strcpy(*response, text);    
//     ESP_LOGI(TAG, "%s", text);
// }
void setText(char **response, const char *text, ...) {
    char dest[1024]; // maximum lenght
    va_list argptr;
    va_start(argptr, text);
    vsprintf(dest, text, argptr);
    va_end(argptr);
    *response = (char*)malloc(strlen(dest)+1);
    strcpy(*response, dest);
    ESP_LOGI(TAG, "%s", dest);
}

esp_err_t getDevicesTree(char **response) {
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }    

    cJSON *devTree = cJSON_CreateArray();
    cJSON *childDevice = devices->child;
    cJSON *devTreeChild = NULL;
    cJSON *devChildren = NULL;    
    cJSON *outputsOut = NULL;
    cJSON *inputsOut = NULL;
    cJSON *inputsChild = NULL;
    cJSON *inputsArray = NULL;
    cJSON *inputOut = NULL;
    cJSON *inputsOutArray = NULL;
    unsigned char count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        // if empty array
        *response = cJSON_Print(devTree);    
        return ESP_OK;
    }
    
    while (childDevice)
    {
        devTreeChild = cJSON_CreateObject();
        cJSON_AddItemToArray(devTree, devTreeChild);
        cJSON_AddNumberToObject(devTreeChild, "slaveid", cJSON_GetObjectItem(childDevice, "slaveid")->valueint);
        cJSON_AddItemToObject(devTreeChild, "type", cJSON_CreateString("device"));        
         cJSON_AddStringToObject(devTreeChild, "name", cJSON_GetObjectItem(childDevice, "name")->valuestring);
        devChildren = cJSON_CreateArray();
        cJSON_AddItemToObject(devTreeChild, "_children", devChildren);        
         outputsOut = cJSON_CreateObject();
         cJSON_AddItemToObject(outputsOut, "type", cJSON_CreateString("outputs"));        
         cJSON_AddItemToObject(outputsOut, "name", cJSON_CreateString("Outputs"));
         cJSON_AddItemToArray(devChildren, outputsOut);
        inputsOut = cJSON_CreateObject();
        cJSON_AddItemToObject(inputsOut, "type", cJSON_CreateString("inputs"));        
         cJSON_AddItemToObject(inputsOut, "name", cJSON_CreateString("Inputs"));
         inputsOutArray = cJSON_CreateArray();
         cJSON_AddItemToObject(inputsOut, "_children", inputsOutArray);        
         cJSON_AddItemToArray(devChildren, inputsOut);
        
         inputsArray = cJSON_GetObjectItem(childDevice, "inputs");
         if (cJSON_IsArray(inputsArray)) {             
             unsigned char count = cJSON_GetArraySize(inputsArray);
            if(count > 0)
            {
                inputsChild = inputsArray->child;
                while (inputsChild) {
                     inputOut = cJSON_CreateObject();
                     cJSON_AddItemToObject(inputOut, "type", cJSON_CreateString("input"));        
                     cJSON_AddItemToObject(inputOut, "name", cJSON_CreateString(cJSON_GetObjectItem(inputsChild, "name")->valuestring));
                     cJSON_AddItemToObject(inputOut, "id", cJSON_CreateNumber(cJSON_GetObjectItem(inputsChild, "id")->valueint));                     
                     cJSON_AddItemToArray(inputsOutArray, inputOut);
                     inputsChild = inputsChild->next;
                 }
            }
         }
        childDevice = childDevice->next;
    }
    *response = cJSON_Print(devTree);
    cJSON_Delete(devTree);
    return ESP_OK;
}

esp_err_t getDevicesAlice(char **response, char* requestId) {
    // for alice
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }    
    uint8_t count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        // if empty array
        *response = "";
        return ESP_OK;
    }

    cJSON *devAlice = cJSON_CreateObject();
    cJSON_AddItemToObject(devAlice, "request_id", cJSON_CreateString(requestId));

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddItemToObject(payload, "user_id", cJSON_CreateString("esp"));

    cJSON *devs = cJSON_CreateArray();

    cJSON *childDevice = devices->child;
    while (childDevice) {
        cJSON *childOutput = cJSON_GetObjectItem(childDevice, "outputs")->child;
        while (childOutput) {
            if (cJSON_IsTrue(cJSON_GetObjectItem(childOutput, "alice"))) {
                cJSON *dev = cJSON_CreateObject();
                //char* idStr = malloc(6);
                char idStr[10] = {'\0'};
                sprintf(idStr, "%d-%d", cJSON_GetObjectItem(childDevice, "slaveid")->valueint, cJSON_GetObjectItem(childOutput, "id")->valueint);
                cJSON_AddItemToObject(dev, "id", cJSON_CreateString(idStr));
                // free(idStr);
                //cJSON_AddItemToObject(dev, "id", cJSON_CreateString(cJSON_GetObjectItem(childDevice, "slaveid")->valuestring)); // TODO : add number
                //cJSON_AddItemToObject(dev, "name", cJSON_GetObjectItem(childOutput, "name"));
                //cJSON_AddItemToObject(dev, "description", cJSON_GetObjectItem(childOutput, "name"));
                //cJSON_AddItemToObject(dev, "room", cJSON_GetObjectItem(childOutput, "room"));
                cJSON_AddItemToObject(dev, "name", cJSON_CreateString(cJSON_GetObjectItem(childOutput, "name")->valuestring));
                cJSON_AddItemToObject(dev, "description", cJSON_CreateString(cJSON_GetObjectItem(childOutput, "name")->valuestring));
                cJSON_AddItemToObject(dev, "room", cJSON_CreateString(cJSON_GetObjectItem(childOutput, "room")->valuestring));

                cJSON_AddItemToObject(dev, "type", cJSON_CreateString("devices.types.light"));
                cJSON *cdata = cJSON_CreateObject();
                cJSON_AddItemToObject(cdata, "slaveid", cJSON_CreateNumber(cJSON_GetObjectItem(childDevice, "slaveid")->valueint));
                cJSON_AddItemToObject(cdata, "output", cJSON_CreateNumber(cJSON_GetObjectItem(childOutput, "id")->valueint));
                cJSON_AddItemToObject(dev, "custom_data", cdata);
                cJSON *capabilities = cJSON_CreateArray();
                cJSON *capability = cJSON_CreateObject();
                cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.on_off"));
                cJSON_AddItemToArray(capabilities, capability);    
                cJSON_AddItemToObject(dev, "capabilities", capabilities);
                cJSON_AddItemToArray(devs, dev);    
            }
            childOutput = childOutput->next;
        }        
        childDevice = childDevice->next;
    }
    // TODO : add DMX devices
    childDevice = DMXdevices->child;
    while (childDevice) {        
        if (cJSON_IsTrue(cJSON_GetObjectItem(childDevice, "alice"))) {
            cJSON *dev = cJSON_CreateObject();            
            cJSON_AddItemToObject(dev, "id", cJSON_CreateString(cJSON_GetObjectItem(childDevice, "id")->valuestring));
            
            cJSON_AddItemToObject(dev, "name", cJSON_CreateString(cJSON_GetObjectItem(childDevice, "name")->valuestring));
            cJSON_AddItemToObject(dev, "description", cJSON_CreateString(cJSON_GetObjectItem(childDevice, "description")->valuestring));
            cJSON_AddItemToObject(dev, "room", cJSON_CreateString(cJSON_GetObjectItem(childDevice, "room")->valuestring));

            cJSON_AddItemToObject(dev, "type", cJSON_CreateString("devices.types.light"));
            cJSON *cdata = cJSON_CreateObject();
            cJSON_AddItemToObject(cdata, "slaveid", cJSON_CreateNumber(cJSON_GetObjectItem(childDevice, "slaveid")->valueint));
            cJSON_AddItemToObject(cdata, "output", cJSON_CreateNumber(cJSON_GetObjectItem(childDevice, "outputid")->valueint));
            cJSON_AddItemToObject(dev, "custom_data", cdata);
            cJSON *capabilities = cJSON_CreateArray();
            // пока константой для всех
            cJSON *capability = cJSON_CreateObject();
            cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.on_off"));
            cJSON_AddItemToArray(capabilities, capability); 

            // brightness
            capability = cJSON_CreateObject();
            cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.range"));
            cJSON_AddItemToObject(capability, "retrievable", cJSON_CreateTrue());
            cJSON *parameters = cJSON_CreateObject();
            cJSON_AddItemToObject(parameters, "instance", cJSON_CreateString("brightness"));
            cJSON_AddItemToObject(parameters, "random_access", cJSON_CreateTrue());
            cJSON_AddItemToObject(parameters, "unit", cJSON_CreateString("unit.percent"));
            cJSON *range = cJSON_CreateObject();
            cJSON_AddItemToObject(range, "max", cJSON_CreateNumber(100));
            cJSON_AddItemToObject(range, "min", cJSON_CreateNumber(0));
            cJSON_AddItemToObject(range, "precision", cJSON_CreateNumber(10));
            cJSON_AddItemToObject(parameters, "range", range);                                    
            cJSON_AddItemToObject(capability, "parameters", parameters);
            cJSON_AddItemToArray(capabilities, capability);

            // hsv
            capability = cJSON_CreateObject();
            cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.color_setting"));
            cJSON_AddItemToObject(capability, "retrievable", cJSON_CreateTrue());
            parameters = cJSON_CreateObject();
            cJSON_AddItemToObject(parameters, "color_model", cJSON_CreateString("hsv"));            
            cJSON *temperature_k = cJSON_CreateObject();
            cJSON_AddItemToObject(temperature_k, "max", cJSON_CreateNumber(4500));
            cJSON_AddItemToObject(temperature_k, "min", cJSON_CreateNumber(2700));
            
            cJSON_AddItemToObject(parameters, "temperature_k", temperature_k);                                    
            cJSON_AddItemToObject(capability, "parameters", parameters);
            cJSON_AddItemToArray(capabilities, capability);
            cJSON_AddItemToObject(dev, "capabilities", capabilities);
            cJSON_AddItemToArray(devs, dev);
        }        
        childDevice = childDevice->next;
    }


    cJSON_AddItemToObject(payload, "devices", devs);
    cJSON_AddItemToObject(devAlice, "payload", payload); 

    *response = cJSON_Print(devAlice);
    cJSON_Delete(devAlice);
    return ESP_OK;
}

// Получение данных устройства по slaveid
// /device?slaveid=1 
esp_err_t getDevice(char **response, uint8_t slaveId) {
    //ESP_LOGI(TAG, "getDevice");
    #define MAXLEN 10000
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {      
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }
    int count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        setErrorText(response, "empty devices array");
        return ESP_FAIL;
    }
    cJSON *childDevice = devices->child;        
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            //*response = cJSON_Print(childDevice);
            //*response = cJSON_PrintUnformatted(childDevice);            
            *response = (char*)malloc(MAXLEN);
            if (!cJSON_PrintPreallocated(childDevice, *response, MAXLEN, 1)) {
                setErrorText(response, "can't cJSON_PrintPreallocated");
                return ESP_FAIL;
            }
            return ESP_OK;
        }
        childDevice = childDevice->next;
    }
    setErrorText(response, "device not found");
    return ESP_FAIL;    
}

uint8_t findDeviceIndex(uint8_t slaveId) {
    // функция находит индекс устройства для последующего удаления или просто для определения его наличия
    uint8_t idx = 0;
    bool found = false;
    cJSON *childDevice = devices->child;        
    while (childDevice)
    {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            cJSON_DeleteItemFromArray(devices, idx);
            found = true;
            break;
        }        
        childDevice = childDevice->next;        
        idx++;
    }
    if (!found)
        idx = 0;
    return idx;
}

// Обновление данных устройства по slaveid
// /device?slaveid=1 
esp_err_t setDevice(char **response, uint8_t slaveId, char *content) {    
    // if slaveid = 0 - new device    
    // content при редактировании приходит весь, т.е. устройство со всеми дочерними объектами, поэтому можно смело удалять и писать заново
    ESP_LOGI(TAG, "setDevice. slaveId %d", slaveId);
    //ESP_LOGI(TAG, "content %s", content);

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsObject(data)) {
        setErrorText(response, "Can't parse content!");
        return ESP_FAIL;
    }
    
    if (!cJSON_IsNumber(cJSON_GetObjectItem(data, "slaveid"))) {
        setErrorText(response, "No slaveId in new data!");
        return ESP_FAIL;
    }
    // name and description are optional fields

    // если slaveId = 0 - то это добавление нового устройства, а если не 0, значит обновление старого
    // нельзя добавить два устройства с одним slaveid
    // соответственно, нельзя обновить slaveid существующее устройства на неуникальный
    uint8_t newSlaveId = cJSON_GetObjectItem(data, "slaveid")->valueint;
    if (newSlaveId != slaveId) {
        // обновление slaveId, надо проверить на дубликат
        if (findDeviceIndex(newSlaveId) > 0) {
            ESP_LOGI(TAG, "Device with slaveid %d already exists.", newSlaveId);
            setErrorText(response, "Device with slaveid already exists");
            cJSON_Delete(data);
            return ESP_FAIL;
        }
    }
    // try to find and delete existing device
    uint8_t idx = findDeviceIndex(newSlaveId);
    if (idx > 0) {
        cJSON_DeleteItemFromArray(devices, idx);
    }
    // adding device as new
    cJSON_AddItemToArray(devices, data);
    setText(response, "OK");
    saveDevices();
    return ESP_OK;      
}

esp_err_t delDevice(char **response, uint8_t slaveId) {   
    // delete device
    ESP_LOGI(TAG, "delDevice");

    if (!cJSON_IsArray(devices) ||
        (cJSON_IsArray(devices) && cJSON_GetArraySize(devices) == 0)) {
        setErrorText(response, "Empty devices");
        return ESP_FAIL;        
    }
    
    if (slaveId == 0) {
        setErrorText(response, "No slaveId");
        return ESP_FAIL;
    }
    
    cJSON *childDevice = devices->child;        

    uint8_t i=0;
    bool found = false;
    while (childDevice)
    {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            cJSON_DeleteItemFromArray(devices, i);
            found = true;
            break;
        }        
        childDevice = childDevice->next;        
        i++;
    }

    if (!found) {
        setErrorText(response, "Device not found!");
        return ESP_FAIL;        
    }

    saveDevices();
    setText(response, "Deleted");    
    return ESP_OK;
}

esp_err_t getDMXDevices(char **response) {
    ESP_LOGI(TAG, "getDMXDevices");
    if (!cJSON_IsArray(DMXdevices)) {      
        //setErrorText(response, "DMXdevices is not a json array");
        *response = cJSON_Print(cJSON_CreateArray());
        return ESP_OK;
    }
    
    *response = cJSON_Print(DMXdevices);
    return ESP_OK;    
}

esp_err_t setDMXDevices(char **response, char *content) {        
    // тупо обновит весь массив устройств
    ESP_LOGI(TAG, "setDMXDevices");

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsArray(data)) {
        setErrorText(response, "Can't parse content. Not json array!");
        return ESP_FAIL;
    }
    
    // TODO : добавить валидацию payload
    if (cJSON_IsObject(DMXdevices)) {
        cJSON_Delete(DMXdevices);
    }
    DMXdevices = data;    
    setText(response, "OK");
    saveDMXDevices();
    return ESP_OK;
}

esp_err_t getTemperatures(char **response) {
    ESP_LOGI(TAG, "getTemperatures");
    if (!cJSON_IsArray(temperaturesData)) {      
        //setErrorText(response, "DMXdevices is not a json array");
        *response = cJSON_Print(cJSON_CreateArray());
        return ESP_OK;
    }
    
    *response = cJSON_Print(temperaturesData);
    return ESP_OK;    
}

esp_err_t setTemperatures(char **response, char *content) {        
    // тупо обновит весь массив 
    ESP_LOGI(TAG, "setTemperatures");

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsArray(data)) {
        setErrorText(response, "Can't parse content. Not json array!");
        return ESP_FAIL;
    }
    
    // TODO : добавить валидацию payload
    if (cJSON_IsObject(temperaturesData)) {
        cJSON_Delete(temperaturesData);
    }
    temperaturesData = data;    
    setText(response, "OK");
    saveTemperatures();
    return ESP_OK;
}

void getTempColors(uint16_t temp, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *w) {
    // TODO : make getter colors
    bool found = false;
    if (cJSON_IsArray(temperaturesData)) {
        cJSON *child = temperaturesData->child;
        while (child) {
            if (cJSON_GetObjectItem(child, "temp")->valueint == temp) {
                *r = cJSON_GetObjectItem(cJSON_GetObjectItem(child, "values"), "r")->valueint;
                *g = cJSON_GetObjectItem(cJSON_GetObjectItem(child, "values"), "g")->valueint;
                *b = cJSON_GetObjectItem(cJSON_GetObjectItem(child, "values"), "b")->valueint;
                *w = cJSON_GetObjectItem(cJSON_GetObjectItem(child, "values"), "w")->valueint;
                found = true;
                break;
            }
            child = child->next;
        }
    }

    if (!found) {
        // default values
        *r = 0;
        *g = 0;
        *b = 0;
        *w = 255;
    }    
}

void processDMXDevices() {
    // выставить значения ДМХ согласно имеющимся данным
    uint8_t r=0,g=0,b=0,s=0,v=0,w=0;
    uint16_t h,temp=2700;    
    uint16_t ra=0,ga=0,ba=0,wa=0;
    cJSON *childDevice = DMXdevices->child;        
    while (childDevice) {
        if (cJSON_IsString(cJSON_GetObjectItem(childDevice, "curMode"))) {
            // проще всего управлять яркостью через модель HSV
            // get brightness
            uint8_t brightness = 0xFF;
            if (cJSON_IsNumber(cJSON_GetObjectItem(childDevice, "brightness"))) {
                brightness = cJSON_GetObjectItem(childDevice, "brightness")->valueint;
            }
            // get dmx channels
            if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "r")))
                ra = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "r")->valueint;
            if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "g")))
                ga = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "g")->valueint;
            if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "b")))
                ba = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "b")->valueint;
            if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "w")))
                wa = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "address"), "w")->valueint;
            
            if (!strcmp(cJSON_GetObjectItem(childDevice, "curMode")->valuestring, "RGB")) {
                // first convert to HSV
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "RGB"), "r")))
                    r = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "RGB"), "r")->valueint;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "RGB"), "g")))
                    g = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "RGB"), "g")->valueint;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "RGB"), "b")))
                    b = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "RGB"), "b")->valueint;
                rgb2hsv(r, g, b, &h, &s, &v);
            } else if (!strcmp(cJSON_GetObjectItem(childDevice, "curMode")->valuestring, "HSV")) {
                // copy values as is
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "HSV"), "h")))
                    h = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "HSV"), "h")->valueint;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "HSV"), "s")))
                    s = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "HSV"), "s")->valueint;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "HSV"), "v")))
                    v = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "HSV"), "v")->valueint;            
            } else if (!strcmp(cJSON_GetObjectItem(childDevice, "curMode")->valuestring, "TEMP")) {
                if (cJSON_IsNumber(cJSON_GetObjectItem(childDevice, "temperature")))
                    temp = cJSON_GetObjectItem(childDevice, "temperature")->valueint;
                // get table values
                getTempColors(temp, &r, &g, &b, &w);
                //ESP_LOGI(TAG, "temp %d, r %d, g %d, b %d, w %d", temp, &r, &g, &b, &w)
                // как менять яркость?
                // 1 вариант - конвертировать в HSV, а канал белого пропорционально уменьшить
                rgb2hsv(r, g, b, &h, &s, &v);
                // вариант 2. принимать значения как РГБ и потом каким-то образом применять яркость, тогда не будет двойного преобразования
            }

            // after correct brightness if it exists
            if (brightness <= 100) {
                v = brightness;
                if (w) {
                    w = w * brightness / 100.0;
                }
            }

            // set final values        
            hsv2rgb(h, s, v, &r, &g, &b);
            setDMXData(ra, r);
            setDMXData(ga, g);
            setDMXData(ba, b);
            setDMXData(wa, w);
        }
        childDevice = childDevice->next;                
    }

}

// Получение списка выходов со статусом
// /outputs?slaveid=2
esp_err_t getOutputs(char **response, unsigned char slaveId) {
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }
    unsigned char count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        setErrorText(response, "Empty array");
        return ESP_FAIL;        
    }
    cJSON *childDevice = devices->child;
        
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "outputs"))) {
                *response = cJSON_Print(cJSON_GetObjectItem(childDevice, "outputs"));
            } else {
                *response = cJSON_Print(cJSON_CreateArray());
            }
            return ESP_OK;
        }
        childDevice = childDevice->next;
    }
    // if slaveid not found
    setErrorText(response, "No slaveid found");    
    return ESP_FAIL;    
}

// Обновление данных выходов по slaveid для UI!!!
// /device?slaveid=1 
esp_err_t setOutputs(char **response, unsigned char slaveId, char *content) {   
    // set outputs
    // можно тупо удалить все выходы устройства и потом по новой записать массив
    ESP_LOGI(TAG, "setOutputs. DeviceId is %d", slaveId);
    ESP_LOGI(TAG, "content %s", content);
    
    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsArray(data))
    {
        setErrorText(response, "Content is't array");
        return ESP_FAIL;
    }
        
    // validate data
    cJSON *childData = data->child;
    while (childData) {     
        if (!cJSON_IsString(cJSON_GetObjectItem(childData, "name")) || 
            (cJSON_GetObjectItem(childData, "name")->valuestring == NULL)) {
            setErrorText(response, "Property name not set"); // TODO : which id            
            return ESP_FAIL;
        }
        if (!cJSON_IsTrue(cJSON_GetObjectItem(childData, "alice")) &&
            (!cJSON_IsString(cJSON_GetObjectItem(childData, "room")) || 
            (cJSON_GetObjectItem(childData, "room")->valuestring == NULL))) {
            setErrorText(response, "Property room not set");            
            return ESP_FAIL;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItem(childData, "id"))) {            
            setErrorText(response, "Property id not set"); // TODO : which id
            return ESP_FAIL;
        }       
        childData = childData->next;
    }

    cJSON *childDevice = devices->child;        
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            // update data          
            if (!cJSON_IsArray(cJSON_GetObjectItem(childDevice, "outputs"))) {
                cJSON_AddItemToObject(childDevice, "outputs", cJSON_CreateArray());
            }
            // ESP_LOGI(TAG, "data %s", cJSON_Print(data));
            cJSON_ReplaceItemInObject(childDevice, "outputs", data);
            //*response = cJSON_Print(data);
            saveDevices();
            return getOutputs(response, slaveId);                   
        }
        childDevice = childDevice->next;
    }
    setErrorText(response, "Device not found");
    return ESP_FAIL;
}

// Получение списка входов со статусом
// /inputs?slaveid=2
esp_err_t getInputs(char **response, unsigned char slaveId) {
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }
    unsigned char count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        setErrorText(response, "Empty array");
        return ESP_FAIL;        
    }
    cJSON *childDevice = devices->child;
        
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "inputs"))) {
                *response = cJSON_Print(cJSON_GetObjectItem(childDevice, "inputs"));
            } else {
                *response = cJSON_Print(cJSON_CreateArray());
            }
            return ESP_OK;
        }
        childDevice = childDevice->next;
    }
    // if slaveid not found
    setErrorText(response, "No slaveid found");    
    return ESP_FAIL;        
}

// Обновление данных входов по slaveid
// /device?slaveid=1 
esp_err_t setInputs(char **response, unsigned char slaveId, char *content) {    
    // set inputs
    // Обновить данные по существующим входам, остальные удаляются вместе с правилами
    ESP_LOGI(TAG, "setInputs. Device id %d", slaveId);

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsArray(data))
    {
        setErrorText(response, "Content is't array");
        return ESP_FAIL;
    }
    
    // validate data
    cJSON *childData = data->child;
    while (childData) {     
        if (!cJSON_IsString(cJSON_GetObjectItem(childData, "name")) || 
            (cJSON_GetObjectItem(childData, "name")->valuestring == NULL)) {
            setErrorText(response, "Property name not set"); // TODO : which id
            return ESP_FAIL;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItem(childData, "id"))) {
            setErrorText(response, "Property id not set"); // TODO : which id
            return ESP_FAIL;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItem(childData, "isButton")) &&
            !cJSON_IsBool(cJSON_GetObjectItem(childData, "isButton"))) {
            setErrorText(response, "Property isButton not set"); // TODO : which id
            return ESP_FAIL;
        }
        childData = childData->next;
    }
    
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) == 0) {
        setErrorText(response, "Empty devices");
        return ESP_FAIL;
    }

    // processing data      
    cJSON *childInput;  
    cJSON *childDevice = devices->child;
    cJSON *newInput;        
    uint8_t inputId;
    uint8_t matched = 0;
    uint8_t inputNum = 0;
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            // device found. processing update/delete data
            ESP_LOGI(TAG, "Device %d found", slaveId);
            if (!cJSON_IsArray(cJSON_GetObjectItem(childDevice, "inputs"))) {
                cJSON_AddItemToObject(childDevice, "inputs", cJSON_CreateArray()); 
            }
            childInput = cJSON_GetObjectItem(childDevice, "inputs")->child;     
            while (childInput) {
                // fetch all inputs of device
                matched = 0;
                ESP_LOGD(TAG, "Current array size %d", cJSON_GetArraySize(cJSON_GetObjectItem(childDevice, "inputs")));
                inputId = cJSON_GetObjectItem(childInput, "id")->valueint;
                childData = data->child;
                while (childData) {
                    // fetch all data inputs 
                    if (cJSON_GetObjectItem(childData, "id")->valueint == inputId) {
                        ESP_LOGD(TAG, "input %d matched", inputId);
                        // input matched. make update
                        cJSON_ReplaceItemInObject(childInput, "name", cJSON_CreateString(cJSON_GetObjectItem(childData, "name")->valuestring));         
                        //cJSON_ReplaceItemInObject(childInput, "isButton", cJSON_CreateNumber(cJSON_GetObjectItem(childData, "isButton")->valueint));
                        cJSON_ReplaceItemInObject(childInput, "isButton", cJSON_CreateBool(cJSON_IsTrue(cJSON_GetObjectItem(childData, "isButton"))));
                        matched = 1;
                    }
                    childData = childData->next;
                }
                childInput = childInput->next;
                if (matched == 0) {
                    // if not matched delete input
                    ESP_LOGD(TAG, "Deleting input %d", inputId);
                    cJSON_DeleteItemFromArray(cJSON_GetObjectItem(childDevice, "inputs"), inputNum);
                    inputNum--;                 
                }               
                inputNum++;
            }
            
            // insert new data (only with isNew attribute)
            ESP_LOGD(TAG, "Insert new data check");
            childData = data->child;
            while (childData) { 
                // fetch all data inputs 
                if (cJSON_IsTrue(cJSON_GetObjectItem(childData, "isNew"))) {
                    ESP_LOGD(TAG, "new input");
                    // make insert
                    newInput = cJSON_CreateObject();
                    cJSON_AddItemToObject(newInput, "id", cJSON_CreateNumber(cJSON_GetObjectItem(childData, "id")->valueint)); 
                    cJSON_AddItemToObject(newInput, "isButton", cJSON_CreateNumber(cJSON_GetObjectItem(childData, "isButton")->valueint)); 
                    cJSON_AddItemToObject(newInput, "name", cJSON_CreateString(cJSON_GetObjectItem(childData, "name")->valuestring)); 
                    cJSON_AddItemToArray(cJSON_GetObjectItem(childDevice, "inputs"), newInput);
                }
                childData = childData->next;
            }
        }
        childDevice = childDevice->next;
    }

    //setText(response, "OK");
    //return ESP_OK;
    saveDevices();
    return getInputs(response, slaveId);     
}

// Получение списка событий
// /events?slaveid=1&inputid=2
esp_err_t getEvents(char **response, unsigned char slaveId, unsigned char inputId) {
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }
    unsigned char count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        setErrorText(response, "Empty array");
        return ESP_FAIL;
    }
    cJSON *childDevice = devices->child;
    cJSON *childInput;
        
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "inputs"))) {
                // find input
                childInput = cJSON_GetObjectItem(childDevice, "inputs")->child;
                while (childInput) {
                    if (cJSON_GetObjectItem(childInput, "id")->valueint == inputId) {
                        // input found, get events
                        if (cJSON_IsArray(cJSON_GetObjectItem(childInput, "events"))) {
                            *response = cJSON_Print(cJSON_GetObjectItem(childInput, "events")); 
                        } else {
                            *response = cJSON_Print(cJSON_CreateArray());
                        }
                        return ESP_OK;
                    }
                    childInput = childInput->next;
                }
                // if not input found then return error             
                setErrorText(response, "No input for slaveid found");
                ESP_LOGE(TAG, "No input %d for slaveid %d found", inputId, slaveId);
                return ESP_FAIL;    
            } else {
                *response = cJSON_Print(cJSON_CreateArray());                
                return ESP_OK;
            }
        }
        childDevice = childDevice->next;
    }
    // if slaveid not found
    //ESP_LOGE(TAG, "No slaveid %d found", slaveId);
    setErrorText(response, "No slaveid found");    
    return ESP_FAIL;
}

// Обновление данных выходов по slaveid и inputid
// /device?slaveid=1&inputid=2 
esp_err_t setEvents(char **response, unsigned char slaveId, unsigned char inputId, char *content) { 
    // set events
    ESP_LOGI(TAG, "setEvents");

    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;        
    }

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsArray(data))
    {
        setErrorText(response, "Content is't array");
        return ESP_FAIL;
    }
        
    // validate data
    cJSON *childData = data->child;
    while (childData) {     
        if (!cJSON_IsString(cJSON_GetObjectItem(childData, "name")) || 
            (cJSON_GetObjectItem(childData, "name")->valuestring == NULL)) {
            setErrorText(response, "Property name not set");
            return ESP_FAIL;
        }
        if (!cJSON_IsString(cJSON_GetObjectItem(childData, "action")) || 
            (cJSON_GetObjectItem(childData, "action")->valuestring == NULL)) {
            setErrorText(response, "Property action not set");
            return ESP_FAIL;
        }
        if (!cJSON_IsString(cJSON_GetObjectItem(childData, "event")) || 
            (cJSON_GetObjectItem(childData, "event")->valuestring == NULL)) {
            setErrorText(response, "Property event not set");
            return ESP_FAIL;
        }
        if (!cJSON_IsNumber(cJSON_GetObjectItem(childData, "slaveid"))) {
            setErrorText(response, "Property slaveid not set"); // TODO : which id
            return ESP_FAIL;
        }       
        if (!cJSON_IsNumber(cJSON_GetObjectItem(childData, "output"))) {
            setErrorText(response, "Property output not set"); // TODO : which id
            return ESP_FAIL;
        }       
        childData = childData->next;
    }

    cJSON *childDevice = devices->child;
    cJSON *childInput;
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            // device found. find input
            childInput = cJSON_GetObjectItem(childDevice, "inputs")->child;
            while (childInput) {
                if (cJSON_GetObjectItem(childInput, "id")->valueint == inputId) {
                    // input found. update events
                    if (!cJSON_IsArray(cJSON_GetObjectItem(childInput, "events"))) {
                        cJSON_AddItemToObject(childInput, "events", cJSON_CreateArray());
                    }
                    cJSON_ReplaceItemInObject(childInput, "events", data);                  
                    saveDevices();
                    return getEvents(response, slaveId, inputId);
                }
                childInput = childInput->next;
            }
            // no input found
            setErrorText(response, "No input found"); // TODO : which id
            return ESP_FAIL;            
        }
        childDevice = childDevice->next;
    }
    setErrorText(response, "Device not found");
    return ESP_FAIL;
}

esp_err_t getDevices(char **response) {
    ESP_LOGI(TAG, "getDevices");
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {      
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }
    
    *response = cJSON_Print(devices);
    return ESP_OK;    
}

esp_err_t setDevices(char **response, char *content) {      
    ESP_LOGI(TAG, "setDevices");

    cJSON *data = cJSON_Parse(content);
    if (!cJSON_IsObject(data) && !cJSON_IsArray(data))
    {
        setErrorText(response, "Can't parse content");
        return ESP_FAIL;
    }
    cJSON_Delete(devices);
    devices = data;
    saveDevices();      
    setText(response, "OK");
    return ESP_OK;  
}

uint8_t getActionValue(char * str) {
    if ((!strcmp(str, "on")) || (!strcmp(str, "ON"))) {        
        return ACT_ON;
    }
    if ((!strcmp(str, "off")) || (!strcmp(str, "OFF"))){        
        return ACT_OFF;
    }
    if ((!strcmp(str, "toggle")) || (!strcmp(str, "TOGGLE"))) {        
        return ACT_TOGGLE;
    }
    return ACT_NOTHING;
}

char *getTextState(unsigned char value) {  
    char * res = NULL;
    if (value == 1) {
        res = malloc(3);
        strcpy(res, "ON");
        res[2] = 0;
    } else if (value == 0) {
        res = malloc(4);
        strcpy(res, "OFF");
        res[3] = 0;
    }
    return res;
}

esp_err_t setDeviceConfig(char **response, uint8_t slaveId) {
    #define delay 100
    /*
                             Address  Type  Length  Description
    EEPROM_ADR              1      RW     1     device address if not set by jumpers
    EEPROM_CONFIGINPUTS     4      RW     2     config inputs (1 switch, 0 button)
    EEPROM_INPUTSQTY        7      RW     1     inputs qty
    EEPROM_OUTPUTSQTY       8      RW     1     outputs qty
    EEPROM_CONFIG           9      RW     1     config*
    EEPROM_AUTOTIMEOUT     0xA     RW     1     timeout for auto mode, *100ms
    EEPROM_RULES           0xb     RW    16     rules** for each input. HI off, LO on/trig

    *config bits
    2 - restore outputs on start
    0,1 - mode (0 passive, 1 active, 2 auto)
    */
    // esp_err_t err;    
        
    // get config inputs & outputs
    uint8_t inputsQty = 0;
    uint8_t outputsQty = 0;
    uint16_t inputsConfig = 0xFFFF;
    uint16_t autoTimeout = 0;
    uint16_t config = 0;
    uint8_t id;
    // rules
    uint16_t eventsData[16] = {0};
    uint8_t event1 = 0, event2 = 0;
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            autoTimeout = cJSON_GetObjectItem(childDevice, "autotimeout")->valueint / 100;
            config = cJSON_GetObjectItem(childDevice, "mode")->valueint;
            if (cJSON_IsTrue(cJSON_GetObjectItem(childDevice, "ros"))) {
                setbit(config, 2);
            }
            if (cJSON_IsTrue(cJSON_GetObjectItem(childDevice, "inverse"))) {
                setbit(config, 3);
            }
            // входы
            cJSON *inputs = cJSON_GetObjectItem(childDevice, "inputs")->child;
            while (inputs) {
                inputsQty++;
                id = cJSON_GetObjectItem(inputs, "id")->valueint;
                if (id > 15) {
                    // ерунда какая-то, пропускаем
                    inputs = inputs->next;
                    continue;    
                }
                // config inputs
                if (cJSON_IsTrue(cJSON_GetObjectItem(inputs, "isButton")))
                    clrbit(inputsConfig, id);                
                // rules
                event1 = 0;
                event2 = 0;
                if (cJSON_IsArray(cJSON_GetObjectItem(inputs, "events"))) {
                    cJSON *events = cJSON_GetObjectItem(inputs, "events")->child;
                    while (events) {
                        if (cJSON_GetObjectItem(events, "slaveid")->valueint == slaveId) {
                            ESP_LOGI("setConfig", "slaveid %d, input %d event %s", slaveId, id, cJSON_GetObjectItem(events, "event")->valuestring);
                            // добавить событие только если оно этого же устройства
                            if (!strcmp(cJSON_GetObjectItem(events, "event")->valuestring, "on") || 
                                !strcmp(cJSON_GetObjectItem(events, "event")->valuestring, "toggle")) {
                                // first byte
                                event1 = cJSON_GetObjectItem(events, "output")->valueint;
                                event1 <<= 4;
                                event1 |= getActionValue(cJSON_GetObjectItem(events, "action")->valuestring);
                            } else { // off
                                // second byte
                                event2 = cJSON_GetObjectItem(events, "output")->valueint;
                                event2 <<= 4;
                                event2 |= getActionValue(cJSON_GetObjectItem(events, "action")->valuestring);
                            }                            
                        }                    
                        events = events->next;
                    }       
                    eventsData[id] = event1 << 8;                            
                    eventsData[id] |= event2;
                }         
                inputs = inputs->next;
            }
            // подсчет выходов
            cJSON *outputs = cJSON_GetObjectItem(childDevice, "outputs")->child;
            while (outputs) {                
                outputsQty++;
                outputs = outputs->next;
            }    
            break;        
        }
        childDevice = childDevice->next;
    }
    
    setHoldingQueue(slaveId, 1, slaveId);
    setHoldingQueue(slaveId, 4, inputsConfig);
    setHoldingQueue(slaveId, 7, inputsQty);
    setHoldingQueue(slaveId, 8, outputsQty);
    setHoldingQueue(slaveId, 9, config);
    setHoldingQueue(slaveId, 0x0A, autoTimeout);
    for (uint8_t i=0; i<inputsQty; i++)
        setHoldingQueue(slaveId, 0x0B+i, eventsData[i]);
    
    setText(response, "OK");
    return ESP_OK;
}

esp_err_t getStatus(char **response) {
    //ESP_LOGI(TAG, "getStatus");
    cJSON *status = cJSON_CreateObject();    
    char *uptime = getUpTime();
    char *curdate = getCurrentDateTime("%d.%m.%Y %H:%M:%S");
    char *hostname = getNetworkConfigValueString("hostname");
    cJSON_AddItemToObject(status, "freememory", cJSON_CreateNumber(esp_get_free_heap_size()));
    cJSON_AddItemToObject(status, "uptime", cJSON_CreateString(uptime));
    cJSON_AddItemToObject(status, "curdate", cJSON_CreateString(curdate));
    cJSON_AddItemToObject(status, "devicename", cJSON_CreateString(hostname));
    
    *response = cJSON_Print(status);
    free(uptime);
    free(curdate);    
    cJSON_Delete(status);
    return ESP_OK;    
}

esp_err_t getOutput(char **response, uint8_t slaveId, uint8_t outputId) {
    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;
    }
    uint8_t count = cJSON_GetArraySize(devices);
    if(count < 1)
    {
        setErrorText(response, "Empty array");
        return ESP_FAIL;        
    }
    cJSON *childDevice = devices->child;
    cJSON *childOutput = NULL;
        
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "outputs"))) {
                // find input
                childOutput = cJSON_GetObjectItem(childDevice, "outputs")->child;
                while (childOutput) {
                    if (cJSON_GetObjectItem(childOutput, "id")->valueint == outputId) {
                        if (cJSON_IsNumber(cJSON_GetObjectItem(childOutput, "curVal"))) {
                            *response = getTextState(cJSON_GetObjectItem(childOutput, "curVal")->valueint);
                            if (*response == NULL) {
                                setErrorText(response, "Bad curVal");
                                return ESP_FAIL;    
                            }                           
                            return ESP_OK;
                        } else {
                            setErrorText(response, "No curVal");
                            return ESP_FAIL;
                        }
                    }
                    childOutput = childOutput->next;
                }
                // if not output found then return error                
                setErrorText(response, "No output for slaveid found");
                ESP_LOGE(TAG, "No output %d for slaveid %d found", outputId, slaveId);
                return ESP_FAIL;    
            }
        }
        childDevice = childDevice->next;
    }
    // if slaveid not found
    setErrorText(response, "No slaveid found");    
    return ESP_FAIL;    
}

esp_err_t setOutput(char **response, uint8_t slaveId, uint8_t outputId, uint8_t action) { 
    // set output или щелкнуть реле 
    // значения 
    ESP_LOGI(TAG, "setOutput"); 
    // convert action to modbus command
    if (action == ACT_TOGGLE) {
        setHoldingQueue(slaveId, 0xCC, outputId);
    } else {
        if (action == ACT_ON)
            action = 1;
        else if (action == ACT_OFF)
            action = 0;        
        setCoilQueue(slaveId, outputId, action);
    }
    setText(response, "OK");
    return ESP_OK;  
}

cJSON* setActionAliceRGB(cJSON *device) {
    // set device and return answer
    // {"id":"lamp-001-xdl","capabilities":[{"type":"devices.capabilities.color_setting","state":{"instance":"temperature_k","value":2700}}]}    
    // {"id":"lamp-001-xdl","capabilities":[{"type":"devices.capabilities.color_setting","state":{"instance":"hsv","value":{"h":135,"s":96,"v":100}}}]}    
    // {"id":"lamp-001-xdl","capabilities":[{"type":"devices.capabilities.color_setting","state":{"instance":"rgb","value":{"r":135,"g":96,"b":100}}}]}    
    // {"id":"lamp-002-xdl","capabilities":[{"type":"devices.capabilities.on_off","state":{"instance":"on","value":true}}],"custom_data":{"output":0,"slaveid":3}}
    // {"id":"lamp-002-xdl","capabilities":[{"type":"devices.capabilities.range","state":{"instance":"brightness","value":50}}],"custom_data":{"output":0,"slaveid":3}}
    char *id = cJSON_GetObjectItem(device, "id")->valuestring;
    // make answer
    cJSON *dev = cJSON_CreateObject();
    cJSON_AddItemToObject(dev, "id", cJSON_CreateString(id));
    cJSON *capabilities = cJSON_CreateArray();

    cJSON *childCapability = cJSON_GetObjectItem(device, "capabilities")->child;        
    while (childCapability) {                
        if (!strcmp(cJSON_GetObjectItem(childCapability, "type")->valuestring, "devices.capabilities.on_off")) {
            // if it on_off switch slaveid and output 
            uint8_t slaveId = cJSON_GetObjectItem(cJSON_GetObjectItem(device, "custom_data"), "slaveid")->valueint;
            uint8_t outputId = cJSON_GetObjectItem(cJSON_GetObjectItem(device, "custom_data"), "output")->valueint;        
            uint8_t action = 0;
            if (cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"))) {
                action = 1;            
            }
            setCoilQueue(slaveId, outputId, action);                     
        } else if (!strcmp(cJSON_GetObjectItem(childCapability, "type")->valuestring, "devices.capabilities.color_setting")) {
            if (!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "instance")->valuestring, "hsv")) {
                uint16_t h = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "h")->valueint;
                uint8_t s = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "s")->valueint;
                uint8_t v = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "v")->valueint;
                // find device and set the values                
                cJSON *childDMXDevice = DMXdevices->child;
                while (childDMXDevice) {
                    if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "id")->valuestring, id)) {
                        // set HSV values
                        cJSON *hsv = cJSON_CreateObject();
                        cJSON_AddItemToObject(hsv, "h", cJSON_CreateNumber(h));
                        cJSON_AddItemToObject(hsv, "s", cJSON_CreateNumber(s));
                        cJSON_AddItemToObject(hsv, "v", cJSON_CreateNumber(v));
                        // if (cJSON_IsObject(cJSON_GetObjectItem(childDMXDevice, "HSV"))) {
                        //     ESP_LOGW(TAG, "HSW is object, deleting");                            
                        //     cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "HSV"));
                        // }
                        cJSON_ReplaceItemInObject(childDMXDevice, "HSV", hsv);
                        // ESP_LOGI(TAG, "DMXdevices %s", cJSON_Print(DMXdevices));
                        //ESP_LOGW(TAG, "hsv %s", cJSON_Print(hsv));
                        // cJSON_AddItemToObject(childDMXDevice, "HSV", hsv);                         
                        // ESP_LOGI(TAG, "DMXdevices %s", cJSON_Print(DMXdevices));
                        // if (!cJSON_IsInvalid(cJSON_GetObjectItem(childDMXDevice, "curMode"))) {
                        //     ESP_LOGW(TAG, "curMode isn't cJSON_IsInvalid, deleting");        
                        //     cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "curMode"));                            
                        // }
                        // ESP_LOGI(TAG, "DMXdevices %s", cJSON_Print(DMXdevices));
                        //cJSON_AddItemToObject(childDMXDevice, "curMode", cJSON_CreateString("HSV"));                         
                        // cJSON_AddStringToObject(childDMXDevice, "curMode", "HSV");
                        cJSON_ReplaceItemInObject(childDMXDevice, "curMode", cJSON_CreateString("HSV"));
                        break;
                    }
                    childDMXDevice = childDMXDevice->next;
                }
            } else if (!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "instance")->valuestring, "rgb")) {
                uint8_t r=0, g=0, b=0;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "r")))
                    r = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "r")->valueint;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "g")))
                    g = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "g")->valueint;
                if (cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "b")))
                    b = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value"), "b")->valueint;
                // find device and set the values
                cJSON *childDMXDevice = DMXdevices->child;
                while (childDMXDevice) {
                    if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "id")->valuestring, id)) {
                        // set RGB values                        
                        cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "RGB"));
                        cJSON *rgb = cJSON_CreateObject();
                        cJSON_AddItemToObject(rgb, "r", cJSON_CreateNumber(r));
                        cJSON_AddItemToObject(rgb, "g", cJSON_CreateNumber(g));
                        cJSON_AddItemToObject(rgb, "b", cJSON_CreateNumber(b));
                        // cJSON_AddItemToObject(childDMXDevice, "RGB", rgb); 
                        cJSON_ReplaceItemInObject(childDMXDevice, "RGB", rgb);
                        // cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "curMode"));
                        // cJSON_AddItemToObject(childDMXDevice, "curMode", cJSON_CreateString("RGB"));                         
                        cJSON_ReplaceItemInObject(childDMXDevice, "curMode", cJSON_CreateString("RGB"));
                        break;
                    }
                    childDMXDevice = childDMXDevice->next;
                }
            } else if (!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "instance")->valuestring, "temperature_k")) {
                uint16_t value = cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value")->valueint;
                // find device and set the values
                cJSON *childDMXDevice = DMXdevices->child;
                while (childDMXDevice) {
                    if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "id")->valuestring, id)) {
                        // set Temperature value                        
                        if (cJSON_IsNumber(cJSON_GetObjectItem(childDMXDevice, "temperature")))                       
                            cJSON_ReplaceItemInObject(childDMXDevice, "temperature", cJSON_CreateNumber(value));
                        else
                            cJSON_AddItemToObject(childDMXDevice, "temperature", cJSON_CreateNumber(value));                         

                        
                        // cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "temperature"));
                        // cJSON_AddItemToObject(childDMXDevice, "temperature", cJSON_CreateNumber(value));                        
                        // cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "curMode"));
                        // cJSON_AddItemToObject(childDMXDevice, "curMode", cJSON_CreateString("TEMP"));                         
                        cJSON_ReplaceItemInObject(childDMXDevice, "curMode", cJSON_CreateString("TEMP"));
                        break;
                    }
                    childDMXDevice = childDMXDevice->next;
                }
            }
        } else if (!strcmp(cJSON_GetObjectItem(childCapability, "type")->valuestring, "devices.capabilities.range")) {
            // get brightness 
            uint16_t brightness = cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "value")->valueint;
            // ESP_LOGI(TAG, "brightness new value %d", brightness);
            // find device and set the values
            cJSON *childDMXDevice = DMXdevices->child;
            while (childDMXDevice) {
                if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "id")->valuestring, id)) {
                    // set Temperature value 
                    if (cJSON_IsNumber(cJSON_GetObjectItem(childDMXDevice, "brightness")))                       
                        cJSON_ReplaceItemInObject(childDMXDevice, "brightness", cJSON_CreateNumber(brightness));
                    else
                        cJSON_AddItemToObject(childDMXDevice, "brightness", cJSON_CreateNumber(brightness));                         
                    // cJSON_Delete(cJSON_GetObjectItem(childDMXDevice, "brightness"));
                    // cJSON_AddItemToObject(childDMXDevice, "temperature", cJSON_CreateNumber(brightness));                        
                    break;
                }
                childDMXDevice = childDMXDevice->next;
            }
        }   
        // answer
        cJSON *capability = cJSON_CreateObject();
        cJSON_AddItemToObject(capability, "type", cJSON_CreateString(cJSON_GetObjectItem(childCapability, "type")->valuestring));        
        cJSON *state = cJSON_CreateObject();
        cJSON_AddItemToObject(state, "instance", cJSON_CreateString(cJSON_GetObjectItem(cJSON_GetObjectItem(childCapability, "state"), "instance")->valuestring));
        cJSON *action_result = cJSON_CreateObject();
        cJSON_AddItemToObject(action_result, "status", cJSON_CreateString("DONE"));
        cJSON_AddItemToObject(state, "action_result", action_result);        
        cJSON_AddItemToObject(capability, "state", state);        
        cJSON_AddItemToArray(capabilities, capability);

        childCapability = childCapability->next;
    }    
    // ESP_LOGW(TAG, "DMXdevices %s", cJSON_Print(DMXdevices));
    saveDMXDevices();
    cJSON_AddItemToObject(dev, "capabilities", capabilities);
    return dev;
}   

cJSON* setActionAliceSlave(cJSON *device) {
    // set data for modbus slave
    // get slaveid and output
    uint8_t slaveId = cJSON_GetObjectItem(cJSON_GetObjectItem(device, "custom_data"), "slaveid")->valueint;
    uint8_t outputId = cJSON_GetObjectItem(cJSON_GetObjectItem(device, "custom_data"), "output")->valueint;
    uint8_t action = 0;
    cJSON *capabilities = cJSON_GetObjectItem(device, "capabilities")->child;   
    while (capabilities) {
        if (!strcmp(cJSON_GetObjectItem(capabilities, "type")->valuestring, "devices.capabilities.on_off")) {
            if (cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(capabilities, "state"), "value"))) {
                action = 1;
                break;
            }
        }
        capabilities = capabilities->next;
    }
    setCoilQueue(slaveId, outputId, action);     
    
    cJSON *dev = cJSON_CreateObject();
    cJSON_AddItemToObject(dev, "id", cJSON_CreateString(cJSON_GetObjectItem(device, "id")->valuestring));
    cJSON *status = cJSON_CreateObject();
    cJSON_AddItemToObject(status, "status", cJSON_CreateString("DONE")); // пока так, т.к. сразу ответ не придет, единственное можно проверить на наличие слейва и выхода в нем
    cJSON_AddItemToObject(dev, "action_result", status);        
        
    // cJSON_AddItemToObject(dev, "type", cJSON_CreateString("devices.capabilities.on_off"));
    // cJSON_AddItemToObject(dev, "state", state);
    return dev;    
}

bool isRGBDevice(char* id) {
    cJSON *childDevice = DMXdevices->child;        
    while (childDevice) {
        if (!strcmp(cJSON_GetObjectItem(childDevice, "id")->valuestring, id)) {
            return true;
        }        
        childDevice = childDevice->next;                
    }    
    return false;
}

esp_err_t setActionAlice(char **response, char *content, char* requestId) { 
    // выставить значение устройства алисы    
    ESP_LOGI(TAG, "setActionAlice");     
    
    cJSON *payload = cJSON_Parse(content);
    if(!cJSON_IsObject(payload))
    {
        setErrorText(response, "No payload!");
        return ESP_FAIL;
    }
    //ESP_LOGI(TAG, "payload %s", cJSON_Print(payload));
    //ESP_LOGI(TAG, "isarray %d", cJSON_IsArray(cJSON_GetObjectItem(payload, "devices")));
    // prepare answer
    cJSON *devAlice = cJSON_CreateObject();
    cJSON_AddItemToObject(devAlice, "request_id", cJSON_CreateString(requestId));

    cJSON *payloadResponse = cJSON_CreateObject();
    cJSON *devs = cJSON_CreateArray(); 
    cJSON *dev = NULL;   
    
    cJSON *childDevice = cJSON_GetObjectItem(cJSON_GetObjectItem(payload, "payload"), "devices")->child;            
    while (childDevice) {
        // check if it RGB device
        if (cJSON_IsString(cJSON_GetObjectItem(childDevice, "id"))) {
            if (isRGBDevice(cJSON_GetObjectItem(childDevice, "id")->valuestring)) {
                dev = setActionAliceRGB(childDevice);  
            } else {
                dev = setActionAliceSlave(childDevice);
            }
            // write answer
            cJSON_AddItemToArray(devs, dev);  
        } else {
            // device without id??!!
            ESP_LOGE(TAG, "device without id!");
        }
        childDevice = childDevice->next;
    }
        
    cJSON_AddItemToObject(payloadResponse, "devices", devs);
    cJSON_AddItemToObject(devAlice, "payload", payloadResponse); 

    *response = cJSON_Print(devAlice);
    cJSON_Delete(payload);
    cJSON_Delete(devAlice);        
    return ESP_OK;  
}

uint8_t getOutputState(uint8_t slaveId, uint8_t outputId) {
    // получить состояние выхода устройства
    // TODO : переделать, добавить проверку если выход не найден - ошибку вернуть
    uint8_t res = 0;
    cJSON *childOutput = NULL;
    cJSON *childDevice = devices->child;    
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "outputs"))) {                
                childOutput = cJSON_GetObjectItem(childDevice, "outputs")->child;
                while (childOutput) {
                    if (cJSON_GetObjectItem(childOutput, "id")->valueint == outputId) {
                        if (cJSON_IsNumber(cJSON_GetObjectItem(childOutput, "curVal"))) {
                            res = cJSON_GetObjectItem(childOutput, "curVal")->valueint;
                            break;
                        }
                    }
                    childOutput = childOutput->next;
                }                
            }
        }
        childDevice = childDevice->next;
    }
    return res;
}

esp_err_t getQueryDevicesAlice(char **response, char *content, char* requestId) { 
    // выставить значение устройства алисы    
    ESP_LOGI(TAG, "getQueryDevicesAlice");     
    uint8_t slaveId, outputId, value;

    cJSON *payload = cJSON_Parse(content);
    if(!cJSON_IsObject(payload))
    {
        setErrorText(response, "No payload!");
        return ESP_FAIL;
    }
    
    // prepare answer
    cJSON *devAlice = cJSON_CreateObject();
    cJSON_AddItemToObject(devAlice, "request_id", cJSON_CreateString(requestId));

    cJSON *payloadResponse = cJSON_CreateObject();
    cJSON *devs = cJSON_CreateArray(); 
    cJSON *dev = NULL;       
    cJSON *state = NULL;
    cJSON *capability = NULL;
    cJSON *capabilities = NULL;
    if (!cJSON_IsArray(cJSON_GetObjectItem(payload, "devices"))) {
        cJSON_Delete(payload);
        return ESP_FAIL;
    }
    cJSON *childDevice = cJSON_GetObjectItem(payload, "devices")->child;            
    while (childDevice) {
        // get slaveid and output
        // пока предположим что есть у всех, но потом надо проверку на наличие сделать        
        slaveId = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "custom_data"), "slaveid")->valueint;
        outputId = cJSON_GetObjectItem(cJSON_GetObjectItem(childDevice, "custom_data"), "output")->valueint;
        value = getOutputState(slaveId, outputId);
        state = cJSON_CreateObject();
        cJSON_AddItemToObject(state, "instance", cJSON_CreateString("on"));
        if (value)
            cJSON_AddItemToObject(state, "value", cJSON_CreateTrue());
        else
            cJSON_AddItemToObject(state, "value", cJSON_CreateFalse());

        dev = cJSON_CreateObject();
        cJSON_AddItemToObject(dev, "id", cJSON_CreateString(cJSON_GetObjectItem(childDevice, "id")->valuestring));
        capabilities = cJSON_CreateArray();
        capability = cJSON_CreateObject();        
        cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.on_off"));
        cJSON_AddItemToObject(capability, "state", state);
        cJSON_AddItemToArray(capabilities, capability);    
        // check for DMX devices
        cJSON *childDMXDevice = DMXdevices->child;
        while (childDMXDevice) {
            if (!strcmp(cJSON_GetObjectItem(childDevice, "id")->valuestring, 
                        cJSON_GetObjectItem(childDMXDevice, "id")->valuestring)) {                
                // color_settings
                capability = cJSON_CreateObject();
                cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.color_setting"));
                if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "curMode")->valuestring, "HSV")) {                    
                    state = cJSON_CreateObject();
                    cJSON_AddItemToObject(state, "instance", cJSON_CreateString("hsv"));
                    cJSON *cvalue = cJSON_CreateObject();
                    cJSON_AddItemToObject(cvalue, "h", cJSON_CreateNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDMXDevice, "HSV"), "h")->valueint));
                    cJSON_AddItemToObject(cvalue, "s", cJSON_CreateNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDMXDevice, "HSV"), "s")->valueint));
                    cJSON_AddItemToObject(cvalue, "v", cJSON_CreateNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(childDMXDevice, "HSV"), "v")->valueint));
                    cJSON_AddItemToObject(state, "value", cvalue);
                    cJSON_AddItemToObject(capability, "state", state);    
                } else if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "curMode")->valuestring, "RGB")) {
                    state = cJSON_CreateObject();
                    cJSON_AddItemToObject(state, "instance", cJSON_CreateString("rgb"));
                    uint32_t rgb = cJSON_GetObjectItem(cJSON_GetObjectItem(childDMXDevice, "RGB"), "r")->valueint << 16 |
                                   cJSON_GetObjectItem(cJSON_GetObjectItem(childDMXDevice, "RGB"), "g")->valueint << 8 |
                                   cJSON_GetObjectItem(cJSON_GetObjectItem(childDMXDevice, "RGB"), "b")->valueint;
                    cJSON_AddItemToObject(state, "value", cJSON_CreateNumber(rgb));
                    cJSON_AddItemToObject(capability, "state", state);    
                } else if (!strcmp(cJSON_GetObjectItem(childDMXDevice, "curMode")->valuestring, "TEMP")) {
                    state = cJSON_CreateObject();
                    cJSON_AddItemToObject(state, "instance", cJSON_CreateString("temperature_k"));
                    cJSON_AddItemToObject(state, "value", cJSON_CreateNumber(cJSON_GetObjectItem(childDMXDevice, "temperature")->valueint));                    
                    cJSON_AddItemToObject(capability, "state", state);    
                }
                cJSON_AddItemToArray(capabilities, capability);    
                // brightness
                if (cJSON_IsNumber(cJSON_GetObjectItem(childDMXDevice, "brightness"))) {
                    capability = cJSON_CreateObject();
                    cJSON_AddItemToObject(capability, "type", cJSON_CreateString("devices.capabilities.range"));
                    state = cJSON_CreateObject();
                    cJSON_AddItemToObject(state, "instance", cJSON_CreateString("brightness"));
                    cJSON_AddItemToObject(state, "value", cJSON_CreateNumber(cJSON_GetObjectItem(childDMXDevice, "brightness")->valueint));
                    cJSON_AddItemToObject(capability, "state", state);    
                    cJSON_AddItemToArray(capabilities, capability);    
                }
                break;
            }
            childDMXDevice = childDMXDevice->next;
        }
        cJSON_AddItemToObject(dev, "capabilities", capabilities);
        cJSON_AddItemToArray(devs, dev);
        childDevice = childDevice->next;
    }
        
    cJSON_AddItemToObject(payloadResponse, "devices", devs);
    cJSON_AddItemToObject(devAlice, "payload", payloadResponse); 

    *response = cJSON_Print(devAlice);
    cJSON_Delete(payload);
    cJSON_Delete(devAlice);        
    return ESP_OK;  
}

esp_err_t switchOutput(char **response, char *content) {       
    // на вход прилетает JSON, в котором описано что делать с конкретным выходом
    ESP_LOGI(TAG, "switchOutput");

    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;        
    }

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsObject(data))
    {
        setErrorText(response, "Content is't a json");
        return ESP_FAIL;
    }

    if (!cJSON_IsNumber(cJSON_GetObjectItem(data, "slaveid"))) {
        setErrorText(response, "No slaveid!");
        return ESP_FAIL;
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItem(data, "output"))) {
        setErrorText(response, "No output!");
        return ESP_FAIL;
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItem(data, "action"))) {
        setErrorText(response, "No action!");
        return ESP_FAIL;
    }        

    return setOutput(response, cJSON_GetObjectItem(data, "slaveid")->valueint,
                     cJSON_GetObjectItem(data, "output")->valueint,
                     cJSON_GetObjectItem(data, "action")->valueint);
}

esp_err_t getServiceConfig(char **response) {
    ESP_LOGI(TAG, "getServiceConfig");
    if (!cJSON_IsObject(serviceConfig) && !cJSON_IsArray(serviceConfig)) {      
        setErrorText(response, "serviceConfig is not a json");
        return ESP_FAIL;
    }
    
    *response = cJSON_Print(serviceConfig);
    return ESP_OK;    
}

esp_err_t setServiceConfig(char **response, char *content) {
    ESP_LOGI(TAG, "setServiceConfig");

    cJSON *parent = cJSON_Parse(content);
    if(!cJSON_IsObject(parent))
    {
        setErrorText(response, "Is not a JSON object");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }
    
    if (!cJSON_IsNumber(cJSON_GetObjectItem(parent, "pollingTime"))) {
        setErrorText(response, "No pollingTime present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItem(parent, "pollingTimeout"))) {
        setErrorText(response, "No pollingTimeout present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItem(parent, "readtimeout"))) {
        setErrorText(response, "No readtimeout present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItem(parent, "savePeriod"))) {
        setErrorText(response, "No savePeriod present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
  //   if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "httpEnable"))) {
  //    setErrorText(response, "No httpEnable present");
  //    cJSON_Delete(parent);
        // return ESP_FAIL; 
  //   }    
    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "actionslaveproc"))) {
        setErrorText(response, "No actionslaveproc present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "httpsEnable"))) {
        setErrorText(response, "No httpsEnable present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "authEnable"))) {
        setErrorText(response, "No authEnable present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsString(cJSON_GetObjectItem(parent, "authUser"))) {
        setErrorText(response, "No authUser present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsString(cJSON_GetObjectItem(parent, "authPass"))) {
        setErrorText(response, "No authPass present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "wdteth"))) {
        setErrorText(response, "No wdteth present");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }
    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "wdtmem"))) {
        setErrorText(response, "No wdtmem present");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }
    if (!cJSON_IsNumber(cJSON_GetObjectItem(parent, "wdtmemsize"))) {
        setErrorText(response, "No wdtmemsize present");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }

    cJSON_Delete(serviceConfig);
    serviceConfig = parent;
    saveServiceConfig();
    setText(response, "OK");    
    return ESP_OK;
}

esp_err_t getNetworkConfig(char **response) {
    ESP_LOGI(TAG, "getNetworkConfig");
    if (!cJSON_IsObject(networkConfig) && !cJSON_IsArray(networkConfig)) {      
        setErrorText(response, "networkConfig is not a json");
        return ESP_FAIL;
    }    
    *response = cJSON_Print(networkConfig);
    return ESP_OK;    
}

esp_err_t setNetworkConfig(char **response, char *content) {
    cJSON *parent = cJSON_Parse(content);
    bool isEth, isWifi; // требовать ли адреса етх или вифи
    if(!cJSON_IsObject(parent))
    {
        setErrorText(response, "Is not a JSON object");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }

    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "ethdhcp"))) {
        setErrorText(response, "No eth dhcp present");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }
    if (!cJSON_IsBool(cJSON_GetObjectItem(parent, "wifidhcp"))) {
        setErrorText(response, "No wifi dhcp present");
        cJSON_Delete(parent);
        return ESP_FAIL;
    }

    if (!cJSON_IsNumber(cJSON_GetObjectItem(parent, "networkmode"))) {
        setErrorText(response, "No networkmode present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    } else {
        isEth = cJSON_GetObjectItem(parent, "networkmode")->valueint == 2 ||
                cJSON_GetObjectItem(parent, "networkmode")->valueint == 0;
        isWifi = cJSON_GetObjectItem(parent, "networkmode")->valueint == 2 ||
                cJSON_GetObjectItem(parent, "networkmode")->valueint == 1;
        // если выбран етх и включен дхцп то не требовать адреса
        if (isEth && cJSON_IsTrue(cJSON_GetObjectItem(parent, "ethdhcp")))
            isEth = false;
        if (isWifi && cJSON_IsTrue(cJSON_GetObjectItem(parent, "wifidhcp")))
            isWifi = false;
    }

    if (!cJSON_IsString(cJSON_GetObjectItem(parent, "ethip"))) {
        if (isEth) {
            setErrorText(response, "No eth ip present");
            cJSON_Delete(parent);        
            return ESP_FAIL;    
        }
    } else if (!isIp_v4(cJSON_GetObjectItem(parent, "ethip")->valuestring)) {
        setErrorText(response, "Eth IP is invalid");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }

    if (!cJSON_IsString(cJSON_GetObjectItem(parent, "ethnetmask"))) {
        if (isEth) {
            setErrorText(response, "No netmask present");
            cJSON_Delete(parent);
            return ESP_FAIL;    
        }
    } else if (!isIp_v4(cJSON_GetObjectItem(parent, "ethnetmask")->valuestring)) {
        setErrorText(response, "IP netmask invalid");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
     
    if (!cJSON_IsString(cJSON_GetObjectItem(parent, "ethgateway"))) {
        if (isEth) {
            setErrorText(response, "No gateway present");
            cJSON_Delete(parent);
            return ESP_FAIL;    
        }
    } else if (!isIp_v4(cJSON_GetObjectItem(parent, "ethgateway")->valuestring)) {
        setErrorText(response, "IP gateway invalid");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    
    if (isWifi && (!cJSON_IsString(cJSON_GetObjectItem(parent, "wifi_ssid")))) {
        setErrorText(response, "No wifi_ssid present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    } 

    if (isWifi && (!cJSON_IsString(cJSON_GetObjectItem(parent, "wifi_pass")))) {
        setErrorText(response, "No wifi_pass present");
        cJSON_Delete(parent);
        return ESP_FAIL;    
    }
    
    cJSON_Delete(networkConfig);
    networkConfig = parent;    
    saveNetworkConfig();
    setText(response, "OK");
    return ESP_OK;
}

esp_err_t factoryReset() {
    cJSON_Delete(devices);
    devices = cJSON_CreateArray();
    cJSON_Delete(networkConfig);
    createNetworkConfig();    
    saveNetworkConfig();  
    cJSON_Delete(serviceConfig);
    createServiceConfig();
    saveServiceConfig();        
    return ESP_OK;
}

esp_err_t setFactoryReset(char **response) {     
    ESP_LOGI(TAG, "setFactoryReset"); 
    factoryReset();
    setText(response, "OK");
    return ESP_OK;  
}

esp_err_t uiRouter(httpd_req_t *req) {
    char *uri = getClearURI(req->uri);
    char *response = NULL;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    uint8_t slaveId = 0;
    uint8_t inputId = 0;
    uint8_t outputId = 0;
    char *content = NULL;
    
    if ((!strcmp(uri, "/ui/devicesTree")) && (req->method == HTTP_GET)) {
        httpd_resp_set_type(req, "application/json");
        err = getDevicesTree(&response);
    } else if (!strcmp(uri, "/ui/device")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK)/* && (slaveId > 0)*/) {
            if (req->method == HTTP_GET) {
                httpd_resp_set_type(req, "application/json");
                err = getDevice(&response, slaveId);            
            } else if (req->method == HTTP_POST) {
                err = getContent(&content, req);
                if (err == ESP_OK) {
                    err = setDevice(&response, slaveId, content);    
                }
            }
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }
    // вообще непонятно зачем device и devices. Вроде одного должно было хватить    
    } else if (!strcmp(uri, "/ui/devices")) {
        if (req->method == HTTP_GET) {
            httpd_resp_set_type(req, "application/json");
            err = getDevices(&response);
        } else if (req->method == HTTP_POST) {
            err = getContent(&content, req);
            if (err == ESP_OK) {
                err = setDevices(&response, content);    
            }
        }        
    } else if ((!strcmp(uri, "/ui/delDevice")) && (req->method == HTTP_POST)) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            err = delDevice(&response, slaveId);            
            //httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }        
    } else if (!strcmp(uri, "/ui/outputs")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            if (req->method == HTTP_GET) {
                httpd_resp_set_type(req, "application/json");
                err = getOutputs(&response, slaveId);                        
            } else if (req->method == HTTP_POST) {
                err = getContent(&content, req);
                if (err == ESP_OK) {
                    err = setOutputs(&response, slaveId, content);    
                }
            }
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }
    } else if (!strcmp(uri, "/ui/inputs")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            if (req->method == HTTP_GET) {
                httpd_resp_set_type(req, "application/json");
                err = getInputs(&response, slaveId);                        
            } else if (req->method == HTTP_POST) {
                err = getContent(&content, req);
                if (err == ESP_OK) {
                    err = setInputs(&response, slaveId, content);    
                }
            }
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }
    } else if (!strcmp(uri, "/ui/events")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            if ((toDecimal(getParamValue(req, "inputid"), &inputId) == ESP_OK)) {
                if (req->method == HTTP_GET) {
                    httpd_resp_set_type(req, "application/json");
                    err = getEvents(&response, slaveId, inputId);
                } else if (req->method == HTTP_POST) {
                    err = getContent(&content, req);
                    if (err == ESP_OK) {
                        err = setEvents(&response, slaveId, inputId, content);    
                    }
                }
            } else {
                err = ESP_FAIL;
                setErrorText(&response, "No inputid");            
            }            
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }
    } else if ((!strcmp(uri, "/ui/setDeviceConfig")) && (req->method == HTTP_POST)) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            httpd_resp_set_type(req, "application/json");
            err = setDeviceConfig(&response, slaveId);                        
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }        
    } else if ((!strcmp(uri, "/ui/status")) && (req->method == HTTP_GET)) {
        httpd_resp_set_type(req, "application/json");
        err = getStatus(&response);
    } else if (!strcmp(uri, "/ui/output")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            if ((toDecimal(getParamValue(req, "output"), &outputId) == ESP_OK)) {
                if (req->method == HTTP_GET) {
                    httpd_resp_set_type(req, "application/json");
                    err = getOutput(&response, slaveId, outputId);
                } else if (req->method == HTTP_POST) {
                    uint8_t action;
                    if ((toDecimal(getParamValue(req, "action"), &action) == ESP_OK)) {
                        err = setOutput(&response, slaveId, outputId, action);                        
                    } else {
                        setErrorText(&response, "No action");            
                        err = ESP_FAIL;
                    }                    
                }
            } else {
                err = ESP_FAIL;
                setErrorText(&response, "No outputid");            
            }            
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }
    } else if (!strcmp(uri, "/ui/switchOutput")) {
        err = getContent(&content, req);
        if (err == ESP_OK) {
            httpd_resp_set_type(req, "application/json");
            err = switchOutput(&response, content);
        }
    } else if (!strcmp(uri, "/service/config/service")) {
        if (req->method == HTTP_GET) {
            httpd_resp_set_type(req, "application/json");                
            err = getServiceConfig(&response);
        } else if (req->method == HTTP_POST) {
            err = getContent(&content, req);
            if (err == ESP_OK) {
                err = setServiceConfig(&response, content);    
            }
        }        
    } else if (!strcmp(uri, "/service/config/network")) {
        if (req->method == HTTP_GET) {
            httpd_resp_set_type(req, "application/json");
            err = getNetworkConfig(&response);
        } else if (req->method == HTTP_POST) {
            err = getContent(&content, req);
            if (err == ESP_OK) {
                err = setNetworkConfig(&response, content);    
            }
        }        
    } else if ((!strcmp(uri, "/service/config/factoryReset")) && (req->method == HTTP_POST)) {
        if (getParamValue(req, "reset") != NULL) {
            err = setFactoryReset(&response);
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No reset");            
        }        
    } else if ((!strcmp(uri, "/service/reboot")) && (req->method == HTTP_POST)) {
        // TODO : create deffered task for reboot
        if (getParamValue(req, "reboot") != NULL) {
            ESP_LOGW(TAG, "Reebot!!!");
            //esp_restart();
            reboot = true;
            setText(&response, "OK");
            err = ESP_OK;
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No reboot");            
        }        
    } else if ((!strcmp(uri, "/ui/log")) && (req->method == HTTP_GET)) {
        err = getLogFile(req);
    } else if ((!strcmp(uri, "/service/upgrade")) && (req->method == HTTP_POST)) {
        stopPolling();
        startOTA();        
        setText(&response, "OK");
        err = ESP_OK;        
    } else if ((!strcmp(uri, "/ui/version")) && (req->method == HTTP_GET)) {        
        char *version = getCurrentVersion();
        setText(&response, version);
        free(version);
        httpd_resp_set_type(req, "application/json");
        err = ESP_OK;    
    } else if (((!strcmp(uri, "/v1.0")) || (!strcmp(uri, "/v1.0/"))) && (req->method == HTTP_HEAD)) {
        ESP_LOGI(TAG, "HEAD method");
        httpd_resp_send(req, "online", -1);
        err = ESP_OK;
    } else if ((!strcmp(uri, "/alice/v1.0/user/devices")) && (req->method == HTTP_GET)) {        
        // get X-Request-Id from header
        char* requestId; 
        uint8_t buf_len = httpd_req_get_hdr_value_len(req, "X-Request-Id") + 1;
        if (buf_len > 1) {
            requestId = malloc(buf_len);
            if (httpd_req_get_hdr_value_str(req, "X-Request-Id", requestId, buf_len) == ESP_OK) {
                httpd_resp_set_type(req, "application/json");
                err = getDevicesAlice(&response, requestId);        
            }
            free(requestId);
        } else {
            setErrorText(&response, "No X-Request-Id header!"); 
            err = ESP_FAIL;
        }        
    } else if ((!strcmp(uri, "/alice/v1.0/user/devices/query")) && (req->method == HTTP_POST)) {        
        // get X-Request-Id from header
        char* requestId; 
        uint8_t buf_len = httpd_req_get_hdr_value_len(req, "X-Request-Id") + 1;
        if (buf_len > 1) {
            requestId = malloc(buf_len);
            if (httpd_req_get_hdr_value_str(req, "X-Request-Id", requestId, buf_len) == ESP_OK) {                
                err = getContent(&content, req);
                if (err == ESP_OK) {
                    httpd_resp_set_type(req, "application/json");
                    err = getQueryDevicesAlice(&response, content, requestId);        
                }
            }
            free(requestId);
        } else {
            setErrorText(&response, "No X-Request-Id header!"); 
            err = ESP_FAIL;
        }            
    } else if ((!strcmp(uri, "/alice/v1.0/user/devices/action")) && (req->method == HTTP_POST)) {        
        //get X-Request-Id from header
        char* requestId; 
        uint8_t buf_len = httpd_req_get_hdr_value_len(req, "X-Request-Id") + 1;
        if (buf_len > 1) {
            requestId = malloc(buf_len);
            if (httpd_req_get_hdr_value_str(req, "X-Request-Id", requestId, buf_len) == ESP_OK) {                
                err = getContent(&content, req);
                if (err == ESP_OK) {
                    httpd_resp_set_type(req, "application/json");
                    err = setActionAlice(&response, content, requestId);
                }                
            }
            free(requestId);            
        } else {
            setErrorText(&response, "No X-Request-Id header!"); 
            err = ESP_FAIL;
        }               
        
    } else if (!strncmp(uri, "/alice/", 7)) {        
        setText(&response, req->uri);
        err = ESP_ERR_NOT_FOUND;        
    } else if (!strcmp(uri, "/ui/DMXdevices")) {
        if (req->method == HTTP_GET) {
            httpd_resp_set_type(req, "application/json");
            err = getDMXDevices(&response);
        } else if (req->method == HTTP_POST) {
            err = getContent(&content, req);
            if (err == ESP_OK) {
                err = setDMXDevices(&response, content);    
            }            
        }
    } else if (!strcmp(uri, "/ui/temperatures")) {
        if (req->method == HTTP_GET) {
            httpd_resp_set_type(req, "application/json");
            err = getTemperatures(&response);
        } else if (req->method == HTTP_POST) {
            err = getContent(&content, req);
            if (err == ESP_OK) {
                err = setTemperatures(&response, content);    
            }            
        }
    }



    // check result
    if (err == ESP_OK) {
        httpd_resp_set_status(req, "200");        
    } else if (err == ESP_ERR_NOT_FOUND) {
        httpd_resp_set_status(req, "404");        
        setErrorText(&response, "Method not found!");        
        //httpd_resp_send(req, "Not found!"); //req->uri, strlen(req->uri));
    } else {
        httpd_resp_set_status(req, "400");
    }
    if (response != NULL) {
        httpd_resp_send(req, response, -1);
        free(response);
    }   
    if (content != NULL)
        free(content);
    free(uri);

    return ESP_OK;
}

void setDeviceOutputValues(uint8_t slaveid, uint16_t values, uint8_t offset, uint8_t size) {
    // выставить выходы конкретного устойства в массиве устройств devices
    // values - значения для выходов без учета сдвига
    ESP_LOGD(TAG, "setDeviceOutputValues slaveid %d values %d offset %d", slaveid, values, offset);
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveid) {
            // ESP_LOGI(TAG, "Found slaveid %d for set outputs. values is %d, offset if %d, size is %d", slaveid, values, offset, size);
            cJSON *outputs = cJSON_GetObjectItem(child, "outputs");
            if (cJSON_IsArray(outputs)) {
                cJSON *output = outputs->child;
                while (output) {                    
                    if ((cJSON_GetObjectItem(output, "id")->valueint >= offset) && 
                        (cJSON_GetObjectItem(output, "id")->valueint < offset+size)) {
                        uint8_t value = ((values >> (cJSON_GetObjectItem(output, "id")->valueint-offset)) & 0x0001);                        
                        if (cJSON_IsNumber(cJSON_GetObjectItem(output, "curVal")))
                            cJSON_ReplaceItemInObject(output, "curVal", cJSON_CreateNumber(value));                      
                        else
                            cJSON_AddItemToObject(output, "curVal", cJSON_CreateNumber(value));
                    }
                    output = output->next;  
                }
            }           
            break;
        }
        child = child->next;
    }
}

void setDeviceOutputValues2(uint8_t slaveid, uint8_t *values) {
    // выставить выходы конкретного устойства в массиве устройств devices    
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveid) {
            // ESP_LOGI(TAG, "Found slaveid %d for set outputs. values is %d, offset if %d, size is %d", slaveid, values, offset, size);
            cJSON *outputs = cJSON_GetObjectItem(child, "outputs");
            if (cJSON_IsArray(outputs)) {
                uint8_t size = cJSON_GetArraySize(outputs);
                cJSON *output = outputs->child;
                while (output) {                    
                    uint8_t id = cJSON_GetObjectItem(output, "id")->valueint;
                    if (id < size) {
                        uint8_t value = (values[id/8] >> (id-8*(id/8))) & 0x01;
                        if (cJSON_IsNumber(cJSON_GetObjectItem(output, "curVal")))
                            cJSON_ReplaceItemInObject(output, "curVal", cJSON_CreateNumber(value));                      
                        else
                            cJSON_AddItemToObject(output, "curVal", cJSON_CreateNumber(value));
                    }
                    output = output->next;  
                }
            }           
            break;
        }
        child = child->next;
    }
}

void setDeviceInputValues(uint8_t slaveid, uint16_t values, uint8_t offset, uint8_t size) {
    // выставить входы конкретного устойства в массиве устройств devices.
    // values - значения для входов без учета сдвига
    ESP_LOGD(TAG, "setDeviceInputValues slaveid %d values %d offset %d", slaveid, values, offset);
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveid) {
            cJSON *inputs = cJSON_GetObjectItem(child, "inputs");
            if (cJSON_IsArray(inputs)) {
                cJSON *input = inputs->child;
                while (input) {
                    if ((cJSON_GetObjectItem(input, "id")->valueint >= offset) && 
                        (cJSON_GetObjectItem(input, "id")->valueint < offset+size)) {
                        // invert for inputs
                        uint8_t value = ((values >> (cJSON_GetObjectItem(input, "id")->valueint-offset)) & 0x0001);
                        if (cJSON_IsNumber(cJSON_GetObjectItem(input, "curVal")))
                            cJSON_ReplaceItemInObject(input, "curVal", cJSON_CreateNumber(value));                      
                        else
                            cJSON_AddItemToObject(input, "curVal", cJSON_CreateNumber(value));
                    }
                    input = input->next;    
                }
            }           
            break;
        }
        child = child->next;
    }
}

void setDeviceInputValues2(uint8_t slaveid, uint8_t *values) {
    // выставить входы конкретного устройства в массиве устройств devices.        
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveid) {
            cJSON *inputs = cJSON_GetObjectItem(child, "inputs");
            if (cJSON_IsArray(inputs)) {
                uint8_t size = cJSON_GetArraySize(inputs);
                cJSON *input = inputs->child;
                while (input) {
                    uint8_t id = cJSON_GetObjectItem(input, "id")->valueint;
                    if (id < size) {
                        // значения лежат по 8 в каждом байте, младший бит в последнем байте                        
                        uint8_t value = (values[id/8] >> (id-8*(id/8))) & 0x01;
                        if (cJSON_IsNumber(cJSON_GetObjectItem(input, "curVal")))
                            cJSON_ReplaceItemInObject(input, "curVal", cJSON_CreateNumber(value));                      
                        else
                            cJSON_AddItemToObject(input, "curVal", cJSON_CreateNumber(value));
                    }
                    input = input->next;    
                }
            }           
            break;
        }
        child = child->next;
    }
}

bool isEquals(const char* str, char event) {
    if ((!strcmp(str, "off")) && (event == EV_OFF))
        return true;
    if ((!strcmp(str, "on")) && (event == EV_ON))
        return true;
    if ((!strcmp(str, "toggle")) && (event == EV_PRESSED)) // was "pressed"
        return true;
    // TODO : add other events
    // LONGPRESSED, DBLPRESSED
    return false;
}

char *getDeviceName(uint8_t slaveId) {
    cJSON *childDevice = devices->child;
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            return cJSON_GetObjectItem(childDevice, "name")->valuestring;
        }
        childDevice = childDevice->next;
    }
    return "unknown";
}

char *getOutputName(uint8_t slaveId, uint8_t outputId) {
    cJSON *childDevice = devices->child;
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "outputs"))) {
                // find output
                cJSON *childOutput = cJSON_GetObjectItem(childDevice, "outputs")->child;
                while (childOutput) {
                    if (cJSON_GetObjectItem(childOutput, "id")->valueint == outputId) {                        
                        return cJSON_GetObjectItem(childOutput, "name")->valuestring;
                    }
                    childOutput = childOutput->next;
                }                                
            }           
        }
        childDevice = childDevice->next;
    }
    return "unknown";
}

char *getInputName(uint8_t slaveId, uint8_t inputId) {
    cJSON *childDevice = devices->child;
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "inputs"))) {
                // find output
                cJSON *childInput = cJSON_GetObjectItem(childDevice, "inputs")->child;
                while (childInput) {
                    if (cJSON_GetObjectItem(childInput, "id")->valueint == inputId) {                        
                        return cJSON_GetObjectItem(childInput, "name")->valuestring;
                    }
                    childInput = childInput->next;
                }                                
            }           
        }
        childDevice = childDevice->next;
    }
    return "unknown";
}

char *getActionName(uint8_t action) {
    switch (action) {
        case 0:
            return "Nothing";
        case 1:
            return "On";
        case 2:
            return "Off";
        case 3:
            return "Toggle";
        default:
            return "Unknown";
    }
}

void doMBSetCoil(uint8_t slaveid, uint8_t coil, uint8_t value) {
    ESP_LOGI(TAG, "doMBSetCoil adr %d, coil %d, value %d", slaveid, coil, value);
    setCoilQueue(slaveid, coil, value);
}

void addAction(action_t pAction) {
    //ESP_LOGI(TAG, "addAction ");
    if (actions_qty > MAX_ACTIONS) {
        ESP_LOGE(TAG, "Actions exceed MAX_ACTIONS size");
        return;
    }
    action_t *act = malloc(sizeof(action_t));
    if (act == NULL) {
        ESP_LOGE(TAG, "Can't malloc action %d", actions_qty);
        return;
    }
    act->slave_addr = pAction.slave_addr;
    act->output = pAction.output;
    act->action = pAction.action;    
    actions[actions_qty++] = act;        
}

void publish(uint8_t slaveId, uint8_t outputId, uint8_t action) {
    // MQTT
    ESP_LOGI(TAG, "MQTT publush slaveId %d, outputId %d, action %d", slaveId, outputId, action);
    char* data = "OFF";
    if (action == ACT_TOGGLE) {
        // узнать какое значение было до, чтобы понять что публиковать
        //ESP_LOGI(TAG, "MQTT current output value is %d", getOutputState(slaveId, outputId));
        if (getOutputState(slaveId, outputId) == 0)
            data = "ON";
    } else if (action == ACT_ON) {
        data = "ON";
    }
    
    char topic[100];
    char buf[5];
    strcpy(topic, getNetworkConfigValueString("hostname"));
    strcat(topic, "/");
    itoa(slaveId, buf, 10);
    strcat(topic, buf);
    strcat(topic, "/");
    itoa(outputId, buf, 10);
    strcat(topic, buf);
    strcat(topic, "\0");    
    // if (getOutputState(slaveId, outputId) == 1)
    //     data = "ON";
    mqttPublish(topic, data);
}

void processActions() {
    // выполнить действия   
    ESP_LOGD(TAG, "processActions qty %d", actions_qty);
    char buf[MSG_BUFFER];
    for (uint8_t i=0; i<actions_qty; i++) {
        // if (actions[i]->slave_addr) {            
        sprintf(buf, "Action on device %s (%d), output %s (%d), action %s (%d)", 
                getDeviceName(actions[i]->slave_addr), actions[i]->slave_addr, 
                getOutputName(actions[i]->slave_addr, actions[i]->output), actions[i]->output, 
                getActionName(actions[i]->action), actions[i]->action);
        writeLog("I", buf);
        // convert to modbus action
        if (actions[i]->action == ACT_TOGGLE) {
            setHoldingQueue(actions[i]->slave_addr, 0xCC, actions[i]->output);
        } else {
            if (actions[i]->action == ACT_ON)
                actions[i]->action = 1;
            else if (actions[i]->action == ACT_OFF)
                actions[i]->action = 0;
            doMBSetCoil(actions[i]->slave_addr, actions[i]->output, actions[i]->action);
        }

        /*            
        else if (action[i]->action == ACT_TOGGLE) // toggle
        {
            // get current value and invert it
            if (getCoilValue(action[i]->slave_addr, action[i]->output) == 1)
                action[i]->action = 0;
            else
                action[i]->action = 1;
        }            
        */            
        // clean up
        free(actions[i]);
    }   
    actions_qty = 0;
}

void processEvent(event_t *event) {
    // обработка одного события
    // нужно найти действия
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) == 0) {
        ESP_LOGE(TAG, "devices is not a json array");
        return;
    }   
    action_t act;
    cJSON *childInput;      
    cJSON *childDevice = devices->child;
    cJSON *childEvent;              
    while (childDevice)
    {       
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == event->slave_addr) {
            ESP_LOGD(TAG, "found slaveid %d", event->slave_addr);
            if (cJSON_IsArray(cJSON_GetObjectItem(childDevice, "inputs"))) {
                // find input
                childInput = cJSON_GetObjectItem(childDevice, "inputs")->child;
                while (childInput) {
                    if (cJSON_GetObjectItem(childInput, "id")->valueint == event->input) {
                        ESP_LOGD(TAG, "found input %d", event->input);
                        // input found, get events
                        if (cJSON_IsArray(cJSON_GetObjectItem(childInput, "events"))) {
                            childEvent = cJSON_GetObjectItem(childInput, "events")->child;
                            while (childEvent) {
                                if (isEquals(cJSON_GetObjectItem(childEvent, "event")->valuestring, event->event)) {
                                    ESP_LOGD(TAG, "found event %d", event->event);
                                    act.slave_addr = cJSON_GetObjectItem(childEvent, "slaveid")->valueint;
                                    act.output = cJSON_GetObjectItem(childEvent, "output")->valueint;
                                    act.action = getActionValue(cJSON_GetObjectItem(childEvent, "action")->valuestring);
                                    publish(act.slave_addr, act.output, act.action);
                                    // do not process action on the same slave
                                    if (getServiceConfigValueBool("actionslaveproc") && (act.slave_addr == event->slave_addr)) {
                                        // do nothing
                                    } else {   
                                        addAction(act);
                                    }
                                }                               
                                childEvent = childEvent->next;                              
                            }                           
                        } 
                        break;
                    }
                    childInput = childInput->next;
                }
                // if not input found then return error             
                break;
            }           
        }
        childDevice = childDevice->next;
    }
}

char* getDeviceStatus(uint8_t slaveId) {
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (!cJSON_IsString(cJSON_GetObjectItem(childDevice, "status"))) {
                return "offline";
            }
            return cJSON_GetObjectItem(childDevice, "status")->valuestring;
        }
        childDevice = childDevice->next;
    }
    return "offline";
}

void changeDevStatus(uint8_t slaveId, char* status) {
    // выставить флаг онлайн устройству
    ESP_LOGD(TAG, "slaveid %d, status %s", slaveId, status);
    char message[MSG_BUFFER];
    sprintf(message, "Device %d changed state to %s", slaveId, status);
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (strcmp(cJSON_GetObjectItem(childDevice, "status")->valuestring, status)) {
                // статус не соответствует, записать в лог
                if (!strcmp(status, "online"))
                    writeLog("I", message);
                else 
                    writeLog("E", message);                
            }
            cJSON_ReplaceItemInObject(childDevice, "status", cJSON_CreateString(status));
        }
        childDevice = childDevice->next;
    }
}

char isEqualsVals(uint8_t *val1, uint8_t *val2, uint8_t len) {
    for (int i=0;i<len;i++)
        if (val1[i] != val2[i])
            return 0;
    return 1;    
}

uint16_t decDeviceWaitingRetries(uint8_t slaveId) {
    // уменьшить счетчик числа ожиданий для оффлайн устройства при поллинге. Вернет оставшееся кол-во ожиданий
    uint16_t curRetries = 0;
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (!cJSON_IsNumber(cJSON_GetObjectItem(childDevice, "waitingRetries"))) {
                // если не было параметра с попытками - создать и установить макс значение
                cJSON_AddItemToObject(childDevice, "waitingRetries", 
                                      cJSON_CreateNumber(getServiceConfigValueInt("waitingRetries")));                
            }
            curRetries = cJSON_GetObjectItem(childDevice, "waitingRetries")->valueint;
            if (curRetries == 0) {
                // если не останется ничего, то ничего не делаем
                return 0;
            }
            cJSON_ReplaceItemInObject(childDevice, "waitingRetries", cJSON_CreateNumber(--curRetries));
            break;
        }
        childDevice = childDevice->next;
    }
    return curRetries;
}

void resetDeviceWaitingRetries(uint8_t slaveId) {
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (!cJSON_IsNumber(cJSON_GetObjectItem(childDevice, "waitingRetries"))) {
                // если не было параметра с попытками - создать и установить макс значение
                cJSON_AddItemToObject(childDevice, "waitingRetries", 
                                      cJSON_CreateNumber(getServiceConfigValueInt("waitingRetries")));
            }            
            cJSON_ReplaceItemInObject(childDevice, "waitingRetries", 
                                      cJSON_CreateNumber(getServiceConfigValueInt("waitingRetries")));
            break;
        }
        childDevice = childDevice->next;
    }
}

uint16_t decDevicePollingRetries(uint8_t slaveId) {
    // уменьшить счетчик числа обращений к усройству. Вернет оставшееся кол-во обращений    
    uint16_t curRetries = 0;
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (!cJSON_IsNumber(cJSON_GetObjectItem(childDevice, "pollingRetries"))) {
                // если не было параметра с попытками - создать и установить макс значение
                cJSON_AddItemToObject(childDevice, "pollingRetries", 
                                      cJSON_CreateNumber(getServiceConfigValueInt("pollingRetries")));                
            }
            curRetries = cJSON_GetObjectItem(childDevice, "pollingRetries")->valueint;
            if (curRetries == 0) {
                // если не останется ничего, то ничего не делаем
                return 0;
            }
            cJSON_ReplaceItemInObject(childDevice, "pollingRetries", cJSON_CreateNumber(--curRetries));
            break;
        }
        childDevice = childDevice->next;
    }
    return curRetries;
}

void resetDevicePollingRetries(uint8_t slaveId) {
    cJSON *childDevice = devices->child;
    while (childDevice) {
        if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
            if (!cJSON_IsNumber(cJSON_GetObjectItem(childDevice, "pollingRetries"))) {
                // если не было параметра с попытками - создать и установить макс значение
                cJSON_AddItemToObject(childDevice, "pollingRetries", 
                                      cJSON_CreateNumber(getServiceConfigValueInt("pollingRetries")));
            }            
            cJSON_ReplaceItemInObject(childDevice, "pollingRetries", 
                                      cJSON_CreateNumber(getServiceConfigValueInt("pollingRetries")));
            break;
        }
        childDevice = childDevice->next;
    }
}

void makePollingList() {
    // формируем список для поллинга
    pollingList[0] = 0;
    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) < 1) {
        return; 
    }
    uint8_t i=0;
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(child, "polling"))) {
            //if (cJSON_GetObjectItem(child, "pollingmode")->valueint & 0x08) {
                pollingList[i++] = cJSON_GetObjectItem(child, "slaveid")->valueint;                
            //}
        }
        child = child->next;
    }
}

uint8_t getPollingMode(uint8_t slaveId) {
    uint8_t pollingMode = 0;
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveId) {
            pollingMode = cJSON_GetObjectItem(child, "pollingmode")->valueint;                
            break;    
        }
        child = child->next;
    }
    return pollingMode;
}

uint16_t getSlaveOutputsValues(uint8_t slaveId) {
    // get outputs values as unisigned int
    uint16_t values = 0;
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveId) {
            cJSON *outputs = cJSON_GetObjectItem(child, "outputs");
            if (cJSON_IsArray(outputs)) {                
                cJSON *output = outputs->child;
                while (output) {
                    uint8_t id = cJSON_GetObjectItem(output, "id")->valueint;
                    if (id < 16) {
                        // вдруг что-то не так...                        
                        if (cJSON_IsNumber(cJSON_GetObjectItem(output, "curVal")) && 
                            cJSON_GetObjectItem(output, "curVal")->valueint) {
                            setbit(values, id);
                        }                            
                    }
                    output = output->next;    
                }
            }
            break;
        }
        child = child->next;
    }
    return values;   
}

uint32_t getSlaveInputsValues(uint8_t slaveId) {
    // get inputs values as unisigned long
    uint32_t values = 0;
    cJSON *child = devices->child;
    while (child) {
        if (cJSON_GetObjectItem(child, "slaveid")->valueint == slaveId) {
            cJSON *inputs = cJSON_GetObjectItem(child, "inputs");
            if (cJSON_IsArray(inputs)) {                
                cJSON *input = inputs->child;
                while (input) {
                    uint8_t id = cJSON_GetObjectItem(input, "id")->valueint;
                    if (id < 32) {
                        // вдруг что-то не так...                        
                        if (cJSON_IsNumber(cJSON_GetObjectItem(input, "curVal")) && 
                            cJSON_GetObjectItem(input, "curVal")->valueint) {
                            setbit(values, id);
                        }                            
                    }
                    input = input->next;    
                }
            }
            break;
        }
        child = child->next;
    }
    return values;   
}

void queryDevice(uint8_t slaveId) {
    // опрос устройства
    // экспериментальный вариант
    // ESP_LOGI(TAG, "queryDevice slaveId %d", slaveId);
    if (!strcmp(getDeviceStatus(slaveId), "offline")) { // (decDevicePollingRetries(slaveId) == 0) {
        // если текущий девайс в оффлайне, то полить реже
        if (decDeviceWaitingRetries(slaveId) > 0) {
            return;
        } else {
            // дождались своей очереди, можно пробовать опрос
            resetDeviceWaitingRetries(slaveId);
        }
    }

    uint8_t *response = NULL;
    heap_caps_check_integrity_all(true);
    esp_err_t res = ESP_FAIL;
    
    uint8_t pollingMode = getPollingMode(slaveId);
    if (pollingMode == 0x04) {
        // polling for owen
        // поллинг без событий
        // 512 регистр выходы 
        // 513-514 входы 
        res = executeModbusCommand(slaveId, MB_READ_HOLDINGS, 512, 3, &response);
        if (res == ESP_OK) {
            changeDevStatus(slaveId, "online");
            resetDevicePollingRetries(slaveId);
            //changeLEDStatus(LED_NORMAL);
            uint8_t inputs[3];
            inputs[0] = response[5];
            inputs[1] = response[4];
            inputs[2] = response[3];
            uint32_t newInputs = (response[3] << 16) | (response[4] << 8) | (response[5]);
            uint32_t oldInputs = getSlaveInputsValues(slaveId);
            uint32_t diff = newInputs ^ oldInputs;
            if (diff) {
                // something changed
                // make events
                for (uint8_t i=0; i<32; i++) {
                    if (diff >> i & 0x01) {
                        event_t *event = malloc(sizeof(event_t));
                        event->slave_addr = slaveId;    
                        event->input = i;
                        if (oldInputs >> i & 0x01)
                            event->event = 2;
                        else
                            event->event = 1;
                        processEvent(event);
                        //processActions();
                        free(event);
                    }
                }
            }

            setDeviceInputValues2(slaveId, inputs);
            uint8_t outputs[2];
            outputs[0] = response[1];
            outputs[1] = response[0];
            setDeviceOutputValues2(slaveId, outputs);
            // добавить событийную логику, основываясь на предыдущих значениях
        } 
    } else if (pollingMode == 0x08) {
        // events polling (my devices)
        // TODO: check for ESP_ERR_TIMEOUT
        res = executeModbusCommand(slaveId, MB_READ_INPUTREGISTERS, 0, 3, &response);
        if (res == ESP_OK) {
            changeDevStatus(slaveId, "online");
            resetDevicePollingRetries(slaveId);
            //changeLEDStatus(LED_NORMAL);
            uint16_t inputs, outputs;        
            event_t *event = malloc(sizeof(event_t));
            event->slave_addr = slaveId;    
            event->input = response[0];
            event->event = response[1];
            outputs = response[2] << 8 | response[3];
            inputs = response[4] << 8 | response[5];        
            setDeviceOutputValues(slaveId, outputs, 0, 16);
            setDeviceInputValues(slaveId, inputs, 0, 16);
            if (event->event > 0) {
                ESP_LOGI(TAG, "Event %d on input %d slaveId %d", event->event, event->input, event->slave_addr);
                processEvent(event);
                //processActions();
            }
            free(event);
            //free(response); 
        }
    }   

    if (res != ESP_OK) {
        // уменьшить счетчик обращений
        if (decDevicePollingRetries(slaveId) == 0) {
            changeDevStatus(slaveId, "offline");    
        }                         
    }
    if (response != NULL) {
        free(response);                
    }
}

void pollingNew() {
    // определить какие устройства надо полить
    static uint8_t curIndx = 0;
    if (pollingList[curIndx] == 0) {
        curIndx = 0;        
        makePollingList();
    }
    uint8_t slaveId = pollingList[curIndx++];
    if (slaveId) {
        queryDevice(slaveId);        
    }

    processActions();
    processQueue(); // накопившиеся действия над выходами
}

SemaphoreHandle_t getSemaphore() {
    return sem_busy;
}

void createSemaphore() {
    sem_busy = xSemaphoreCreateMutex();
}

void statusLEDTask(void *pvParameter) {    
    uint8_t l;
    // led normal  ^^__^^__  0xCC
    // led error   ^_^_^_^_  0xAA
    // led booting ^_^_^___  0xA8
    // led ora     ^^^^____  0xF0
    uint8_t pattern_normal = 0xCC;
    uint8_t pattern_error = 0xAA;
    uint8_t pattern_booting = 0xA8;
    uint8_t pattern_ota = 0xF0;
    uint8_t i=0;
    while(1)
    {
        switch (ledStatus) {
            case LED_BOOTING:
                l = (pattern_booting >> i) & 0x01;
                break;
            case LED_ERROR:
                l = (pattern_error >> i) & 0x01;
                break;
            case LED_NORMAL:
                l = (pattern_normal >> i) & 0x01;    
                break;
            case LED_OTA:
                l = (pattern_ota >> i) & 0x01;    
                break;
            default:
                l = 0;
        }
        
        if (i++ > 7)
            i=0;        
        gpio_set_level(STATUS_LED, l);
        vTaskDelay(1000 / 4 / portTICK_RATE_MS); // every 250ms
    }
}

void initLED() {
    gpio_pad_select_gpio(STATUS_LED);
    gpio_set_direction(STATUS_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(STATUS_LED, 0);
    xTaskCreate(&statusLEDTask, "statusLEDTask", 4096, NULL, 5, NULL);        
}

void changeLEDStatus(uint8_t status) {
    ledStatus = status;
}

void phyPower(bool on_off) {
    #define PHYPWRPIN 17
    gpio_pad_select_gpio(PHYPWRPIN);
    gpio_set_direction(PHYPWRPIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PHYPWRPIN, on_off);
}

// Если девай отвалился, пробуем его опросить Х раз и если не ответил, оффлайн
// По оффлайн девайсам смотреть признак поллинга и полить раз в У времени или попыток и если ответил - выводить в онлайн


/*
esp_err_t setDMXDevice(char **response, char *content) {    
    // add/edit DMXdevice
    // обновит текущее устройство по айди или добавит новое если такого нет    
    ESP_LOGI(TAG, "setDMXDevice");

    if (!cJSON_IsArray(DMXdevices)) {
        setErrorText(response, "DMXdevices is not a json");
        return ESP_FAIL;        
    }

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsObject(data))
    {
        setErrorText(response, "Can't parse content");
        return ESP_FAIL;
    }
    
    char *Id;
    if (!cJSON_IsString(cJSON_GetObjectItem(data, "id"))) {
        setErrorText(response, "No Id");
        return ESP_FAIL;
    } else {
        Id = cJSON_GetObjectItem(data, "id")->valuestring;
    }
    
    // TODO : добавить валидацию payload
    // ищем существующий айди в массиве и удаляем его если найдем, далее просто добавляем все, что есть
    uint8_t idx = 0;
    // bool found = false;
    cJSON *childDevice = DMXdevices->child;        
    while (childDevice) {
        if (!strcmp(cJSON_GetObjectItem(childDevice, "slaveid")->valuestring, Id)) {
            ESP_LOGI(TAG, "Deleting existing device");
            cJSON_DeleteItemFromArray(DMXdevices, idx);            
            // found = true;
            break;
        }        
        childDevice = childDevice->next;        
        idx++;
    }

    // set new DMX device        
    ESP_LOGI(TAG, "Adding new device");
    cJSON_AddItemToArray(devices, data);            
    setText(response, "OK");
    saveDMXDevices();
    return ESP_OK;
}

void setDMXDeviceValueHSV(char* id, uint16_t h, uint8_t s, uint8_t v) {
    cJSON *childDevice = DMXdevices->child;        
    while (childDevice) {
        if (!strcmp(cJSON_GetObjectItem(childDevice, "id")->valuestring, id)) {        
            // if set HSV, convert it to RGB for DMX
            cJSON_DeleteItemFromArray(DMXdevices, idx);
            break;
        }
        childDevice = childDevice->next;                
    }
}

*/