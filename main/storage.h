//storage.h
#include "webServer.h"

void initStorage();
esp_err_t loadTextFile(char * filename, char ** buffer);
esp_err_t saveTextFile(char * filename, char * buffer);
esp_err_t getFileWeb(httpd_req_t *req);
esp_err_t getFileWebPath(req, path);
esp_err_t setFileWeb(httpd_req_t *req);
esp_err_t writeLog(char* type, char* buffer);
esp_err_t getLogFile(httpd_req_t *req);