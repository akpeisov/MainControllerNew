//core.c
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "storage.h"
#include "webServer.h"
#include "modbus.h"
#include "utils.h"
#include "freertos/semphr.h"

static const char *TAG = "CORE";
static cJSON *networkConfig;
static cJSON *serviceConfig;
static cJSON *devices;

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

#define  setbit(var, bit)    ((var) |= (1 << (bit)))
#define  clrbit(var, bit)    ((var) &= ~(1 << (bit)))

typedef enum {
    ACT_NOTHING, // nothing 
    ACT_ON,      // switch on 
    ACT_OFF,     // switch off
    ACT_TOGGLE   // toggle     
} action_type_t;

typedef struct {
    uint8_t slave_addr; // на каком устройстве произошло событие
    uint8_t input;      // на каком входе         
    uint8_t event;      // какое событие event_type_t
} event_t;

typedef struct {
    uint8_t slave_addr; // адрес устройства назначения
    uint8_t output;     // номер выхода           
    uint8_t action;     // действие action_type_t
} action_t;

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
static action_t *action[MAX_ACTIONS];

esp_err_t loadDevices() {
    char * buffer;
    if (loadTextFile("/config/devices.json", &buffer) == ESP_OK) {
        cJSON *parent = cJSON_Parse(buffer);
        if(!cJSON_IsObject(parent) && !cJSON_IsArray(parent))
        {
            free(buffer);
            return ESP_FAIL;
        }
        cJSON_Delete(devices);
        devices = parent;        
    } else {
        ESP_LOGI(TAG, "can't read devices config. creating null config");       
        cJSON_Delete(devices);
        devices = cJSON_CreateArray();        
    }
    free(buffer);
    return ESP_OK;
}

esp_err_t saveDevices() {
    char * dev = cJSON_Print(devices);
    esp_err_t err = saveTextFile("/config/devices.json", dev);
    free(dev);
    return err;
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
    cJSON_AddItemToObject(serviceConfig, "pollingRetries", cJSON_CreateNumber(5));
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
    } else {
        ESP_LOGI(TAG, "can't read serviceconfig. creating default config");
        cJSON_Delete(serviceConfig);
        if (createServiceConfig() == ESP_OK)
            saveServiceConfig();        
    }
    free(buffer);
    // setting uart receive timeout
    //setReadTimeOut(getReadTimeOut()); //readtimeout new // TODO : uncomment for modbus
    return ESP_OK;
}

esp_err_t loadConfig() {
    if ((loadNetworkConfig() == ESP_OK) && 
        (loadServiceConfig() == ESP_OK) && 
        (loadDevices() == ESP_OK)) {
        return ESP_OK;
    }    
    return ESP_FAIL;
}

uint16_t getServiceConfigValueInt(const char* name) {
    return cJSON_GetObjectItem(serviceConfig, name)->valueint;
}

uint16_t getNetworkConfigValueInt(const char* name) {
    return cJSON_GetObjectItem(networkConfig, name)->valueint;
}

bool getNetworkConfigValueBool(const char* name) {
    return cJSON_IsTrue(cJSON_GetObjectItem(networkConfig, name));
}

char *getNetworkConfigValueString(const char* name) {
    return cJSON_GetObjectItem(networkConfig, name)->valuestring;
}

bool getServiceConfigValueBool(const char* name) {
    return cJSON_IsTrue(cJSON_GetObjectItem(serviceConfig, name));
}

bool isReboot() {
    return reboot;
}

void setErrorText(char **response, const char *text) {
    // если надо аллокейтить внутри функции, то надо на вход передать адрес &dst, а внутри через *работать, в хидере **
    *response = (char*)malloc(strlen(text)+1);
    strcpy(*response, text);    
    ESP_LOGE(TAG, "%s", text);
}

void setText(char **response, const char *text) {
    *response = (char*)malloc(strlen(text)+1);
    strcpy(*response, text);    
    ESP_LOGI(TAG, "%s", text);
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

// Обновление данных устройства по slaveid
// /device?slaveid=1 
esp_err_t setDevice(char **response, uint8_t slaveId, char *content) {    
    // edit device
    // if slaveid = 0 - new device
    // хотя можно было и реальный указать, а в функции проверить - если есть - заменить, если нет - добавить как новый
    
    ESP_LOGI(TAG, "setDevice");

    if (!cJSON_IsObject(devices) && !cJSON_IsArray(devices)) {
        setErrorText(response, "devices is not a json");
        return ESP_FAIL;        
    }

    //new data
    cJSON *data = cJSON_Parse(content);
    if(!cJSON_IsObject(data))
    {
        setErrorText(response, "Can't parse content");
        return ESP_FAIL;
    }
    
    if (!cJSON_IsNumber(cJSON_GetObjectItem(data, "slaveid"))) {
        setErrorText(response, "No slaveId");
        return ESP_FAIL;
    }
    // name and description are optional fields

    if (!cJSON_IsArray(devices) || cJSON_GetArraySize(devices) == 0) {
        // create new element device
        devices = cJSON_CreateArray();
        cJSON_AddItemToArray(devices, data);        
        //*response = cJSON_Print(data);
        setText(response, "OK");
        cJSON_Delete(data);
        return ESP_OK;
    }

    // new device
    if (slaveId == 0) {
        // check for already exists
        slaveId = cJSON_GetObjectItem(data, "slaveid")->valueint;
        cJSON *childDevice = devices->child;        
        while (childDevice)
        {   
            if (cJSON_GetObjectItem(childDevice, "slaveid")->valueint == slaveId) {
                // update data
                setErrorText(response, "Device already exists!");
                cJSON_Delete(data);
                return ESP_FAIL;                
            }
            childDevice = childDevice->next;
        }
        ESP_LOGI(TAG, "Adding new device");
        cJSON_AddItemToArray(devices, data);    
        //cJSON_AddItemReferenceToArray(devices, data);   
        //cJSON_Delete(data); // do not delete data!!!        
        //*response = cJSON_Print(data);
        setText(response, "OK");
        saveDevices();
        return ESP_OK;
    }

    // first delete existing device
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

    if (!found) {
        setText(response, "Device not found!");
        cJSON_Delete(data);
        return ESP_FAIL;
    }
    // then add new device   
    cJSON_AddItemToArray(devices, data);        
    //cJSON_Delete(data);    
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

// Обновление данных выходов по slaveid
// /device?slaveid=1 
esp_err_t setOutputs(char **response, unsigned char slaveId, char *content) {   
    // set outputs
    // можно тупо удалить все выходы устройства и потом по новой записать массив
    ESP_LOGI(TAG, "setOutputs");

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
            setErrorText(response, "Property name not set"); // TODO : which id
            //setErrorText(response, sprintf("Property name  %d not set", 123)); // TODO : which id
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
    ESP_LOGI(TAG, "setInputs");

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
    if (!strcmp(str, "on")) {        
        return ACT_ON;
    }
    if (!strcmp(str, "off")) {        
        return ACT_OFF;
    }
    if (!strcmp(str, "toggle")) {        
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
        err = getDevicesTree(&response);
    } else if (!strcmp(uri, "/ui/device")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            if (req->method == HTTP_GET) {
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
            err = setDeviceConfig(&response, slaveId);                        
        } else {
            err = ESP_FAIL;
            setErrorText(&response, "No slaveid");            
        }        
    } else if ((!strcmp(uri, "/ui/status")) && (req->method == HTTP_GET)) {
        err = getStatus(&response);
    } else if (!strcmp(uri, "/ui/output")) {
        if ((toDecimal(getParamValue(req, "slaveid"), &slaveId) == ESP_OK) && (slaveId > 0)) {
            if ((toDecimal(getParamValue(req, "output"), &outputId) == ESP_OK)) {
                if (req->method == HTTP_GET) {
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
    } else if (!strcmp(uri, "/service/config/service")) {
        if (req->method == HTTP_GET) {
            err = getServiceConfig(&response);
        } else if (req->method == HTTP_POST) {
            err = getContent(&content, req);
            if (err == ESP_OK) {
                err = setServiceConfig(&response, content);    
            }
        }        
    } else if (!strcmp(uri, "/service/config/network")) {
        if (req->method == HTTP_GET) {
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
    }

    // check result
    if (err == ESP_OK) {
        httpd_resp_set_status(req, "200");
        httpd_resp_set_type(req, "application/json");                
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

void initEventsActions() {
    // for (int i=0;i<MAX_EVENTS;i++)
    //     // mb_event[i] = NULL;
    //     mb_event[i] = malloc(sizeof(event_t));
    for (int i=0;i<MAX_ACTIONS;i++)
        action[i] = malloc(sizeof(action_t));
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
            if (cJSON_GetObjectItem(child, "pollingmode")->valueint & 0x08) {
                pollingList[i++] = cJSON_GetObjectItem(child, "slaveid")->valueint;
                //addDataPolling(cJSON_GetObjectItem(child, "slaveid")->valueint, MB_READ_INPUTREGISTERS, 0, 3);
            }
        }
        child = child->next;
    }
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

void addAction(action_t pAction) {
    //ESP_LOGI(TAG, "addAction ");
    // action_t *act = malloc(sizeof(action_t));
 //    if (act == NULL) {
 //        ESP_LOGE(TAG, "Can't malloc action %d", actions_qty);
 //        return;
 //    }
 //    act->slave_addr = pAction.slave_addr;
 //    act->output = pAction.output;
 //    act->action = pAction.action;    
    // action[actions_qty++] = act;
    action[actions_qty]->slave_addr = pAction.slave_addr;
    action[actions_qty]->output = pAction.output;
    action[actions_qty]->action = pAction.action;
    actions_qty++;
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

void processActions() {
    // выполнить действия   
    ESP_LOGD(TAG, "processActions qty %d", actions_qty);
    for (uint8_t i=0; i<actions_qty; i++) {
        if (action[i]->slave_addr) {
            char buf[MSG_BUFFER];
            sprintf(buf, "Action on device %s (%d), output %s (%d), action %s (%d)", 
                    getDeviceName(action[i]->slave_addr), action[i]->slave_addr, 
                    getOutputName(action[i]->slave_addr, action[i]->output), action[i]->output, 
                    getActionName(action[i]->action), action[i]->action);
            writeLog("I", buf);
            // convert to modbus action
            if (action[i]->action == ACT_TOGGLE) {
                setHoldingQueue(action[i]->slave_addr, 0xCC, action[i]->output);
            } else {
                if (action[i]->action == ACT_ON)
                    action[i]->action = 1;
                else if (action[i]->action == ACT_OFF)
                    action[i]->action = 0;
                doMBSetCoil(action[i]->slave_addr, action[i]->output, action[i]->action);
            }

            /*
            if (action[i]->action == ACT_OFF)
                action[i]->action = 0;
            else if (action[i]->action == ACT_ON)
                action[i]->action = 1;
            else if (action[i]->action == ACT_TOGGLE) // toggle
            {
                // get current value and invert it
                if (getCoilValue(action[i]->slave_addr, action[i]->output) == 1)
                    action[i]->action = 0;
                else
                    action[i]->action = 1;
            }
            doMBSetCoil(action[i]->slave_addr, action[i]->output, action[i]->action);
            */            
        } else {
            // web action
            //doWEBAction(action[i]->webMethod, action[i]->webURL);
        }
    }
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

void queryDevice(uint8_t slaveId) {
    // опрос устройства
    // экспериментальный вариант
    // ESP_LOGI(TAG, "queryDevice slaveId %d", slaveId);
    uint8_t *response = NULL;
    static uint8_t response1[6], response2[6], response3[6];
    uint16_t inputs, outputs;    
    event_t *event = malloc(sizeof(event_t));
    heap_caps_check_integrity_all(true);
    actions_qty = 0;                
    if (executeModbusCommand(slaveId, MB_READ_INPUTREGISTERS, 0, 3, &response) == ESP_OK) {
        changeDevStatus(slaveId, "online");
        // slaveid, command, start, qty, *response
        //changeDevStatus(slaveId, "online");
        event->slave_addr = slaveId;
        if (response != NULL) {
            event->input = response[0];
            event->event = response[1];
            outputs = response[2] << 8 | response[3];
            inputs = response[4] << 8 | response[5];
            // for logs
            if ((slaveId == 1) && isEqualsVals(response, response1, 6) == 0) {
                memcpy(response1, response, 6);
                ESP_LOGI(TAG, "SlaveId %d Outputs %d Inputs %d. Event %d on input %d", slaveId, outputs, inputs, event->event, event->input);
            }
            if ((slaveId == 2) && isEqualsVals(response, response2, 6) == 0) {
                memcpy(response2, response, 6);
                ESP_LOGI(TAG, "SlaveId %d Outputs %d Inputs %d. Event %d on input %d", slaveId, outputs, inputs, event->event, event->input);
            }
            if ((slaveId == 3) && isEqualsVals(response, response3, 6) == 0) {
                memcpy(response3, response, 6);
                ESP_LOGI(TAG, "SlaveId %d Outputs %d Inputs %d. Event %d on input %d", slaveId, outputs, inputs, event->event, event->input);
            }
            setDeviceOutputValues(slaveId, outputs, 0, 16);
            setDeviceInputValues(slaveId, inputs, 0, 16);
            if (event->event > 0) {
                processEvent(event);
                processActions(); // просто построит очередь, которую потом обработает processQueue
            }
            free(response);        
        } else {
            ESP_LOGE(TAG, "Empty response");
        }
    } else {
        changeDevStatus(slaveId, "offline");
    }        
    free(event);
    // обновление входов/выходов
    // проверка событий, добавление в очередь
    // выполнение действий будет отдельным таском processQueue
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

    processQueue(); // накопившиеся действия над выходами
}

void initCore() {
    initEventsActions();
}

SemaphoreHandle_t getSemaphore() {
    return sem_busy;
}

void createSemaphore() {
    sem_busy = xSemaphoreCreateMutex();
}


/*
else if (!strcmp(uri, "/ui/getPage")) {
        char *page = getParamValue(req, "page");
        if (page == NULL) {
            err = ESP_FAIL;
            setErrorText(&response, "Page not defined");            
        } else {
            char path[FILE_PATH_MAX];
            strcpy(path, "/webUI/new/");    // new/
            strcat(path, page);
            strcat(path, ".html");
            free(page);        
        }       
    }


    char buf[17];    
    itoa(bootId, buf, 10);
    char path[50];
    strcpy(path, "/logs/");    
    strcat(path, buf);
    strcat(path, ".txt");
    return getFileWeb(path, req);
*/
