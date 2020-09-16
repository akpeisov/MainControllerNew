// storage file
// use internal storage, spiffs, sdcard
#include "nvs_flash.h"
#include "nvs.h"

#include "esp_vfs_semihost.h"
#include "esp_vfs_fat.h"

#include "sdcard.h"
#include "spiffs.h"
#include "webServer.h"
#include "utils.h"

#define USE_SD
#define PATH_SIZE 100
#define BUF_SIZE 2048
#define MOUNT_POINT "/storage"
#define MAX_FILE_SIZE   (200*1024) // 200 KB
#define MAX_FILE_SIZE_STR "200KB"

static const char *TAG = "STORAGE";

uint16_t bootId;
char *logPath;

esp_err_t initNVS() {
	esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    return err;
}

esp_err_t storeNumber(const char *key, const uint16_t value) {
    nvs_handle my_handle;
    esp_err_t err;

    // Open
    err = nvs_open("config", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;
    ESP_LOGD(TAG, "Open ok");

    err = nvs_set_u16(my_handle, key, value);

    if (err != ESP_OK) return err;
    ESP_LOGD(TAG, "Store ok");

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) return err;

    ESP_LOGD(TAG, "Commit ok");
    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t restoreNumber(const char *key, uint16_t *value) {
    nvs_handle my_handle;
    esp_err_t err;

    // Open
    err = nvs_open("config", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;
    ESP_LOGD(TAG, "Open ok");

    err = nvs_get_u16(my_handle, key, value);

    if (err != ESP_OK) return err;
    ESP_LOGD(TAG, "Store ok");

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) return err;

    ESP_LOGD(TAG, "Commit ok");
    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

uint16_t getLastId() {
    uint16_t id = 0;
    restoreNumber("lastid", &id);
    id++;
    storeNumber("lastid", id);
    return id;    
}

void initStorage() {
	initNVS();
	#ifdef USE_SD
	if (initSD(MOUNT_POINT) != ESP_OK) {
		ESP_LOGE(TAG, "Can't init SD");
		if (initSPIFFS(MOUNT_POINT) != ESP_OK) {
			ESP_LOGE(TAG, "Can't init SPIFFS");
		}		
	}	
	#else
	if (initSPIFFS(MOUNT_POINT) != ESP_OK) {
			ESP_LOGE(TAG, "Can't init SPIFFS");
	}		
	#endif
        
    char *buf = malloc(5);
    itoa(getLastId(), buf, 10);

    logPath = malloc(16+strlen(MOUNT_POINT));
    strcpy(logPath, MOUNT_POINT);    
    strcat(logPath, "/logs/");
    strcat(logPath, buf);
    strcat(logPath, ".txt");    
    free(buf);
}

esp_err_t loadTextFile(char * filename, char ** buffer) {    
	// аллокейтит буфер и считает весь файл в него
    char path[PATH_SIZE];
    strcpy(path, MOUNT_POINT);    
    strcat(path, filename);    
    ESP_LOGI(TAG, "Opening file path %s", path);
    FILE* f = fopen(path, "r");    
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for reading", path);
        return ESP_FAIL;
    }

    fseek(f, 0L, SEEK_END);
    uint16_t bufSize = ftell(f);
    fseek(f, 0L, SEEK_SET);
    
    *buffer = (char*)malloc(bufSize+1);
    if (*buffer == NULL) {
        ESP_LOGE(TAG, "Can't allocate buffer for file %s", filename);
        return ESP_FAIL;
    } else {
        ESP_LOGD(TAG, "Allocate ok");
    }

    if (fread(*buffer, 1, bufSize, f) < 1) {
        ESP_LOGE(TAG, "Can't fread");
    }
        
    fclose(f);
    ESP_LOGD(TAG, "getTextFile finish");
    return ESP_OK;
}

esp_err_t saveTextFile(char * filename, char * buffer) {
    // save file to filesystem
    uint16_t bufSize = strlen(buffer);
    char path[PATH_SIZE];
    strcpy(path, MOUNT_POINT);    
    strcat(path, filename);    
    ESP_LOGI(TAG, "Saving file path %s", path);
    
    FILE *fd = fopen(path, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file %s", path);        
        return ESP_FAIL;
    }
   
    if (bufSize != fwrite(buffer, 1, bufSize, fd)) {
        /* Couldn't write everything to file!
         * Storage may be full? */
        fclose(fd);
        unlink(path);
        ESP_LOGE(TAG, "File write failed!");
        return ESP_FAIL;
    }
        
    /* Close file upon upload completion */
    fclose(fd);
    ESP_LOGI(TAG, "File saved successfully");

    return ESP_OK;
}

esp_err_t getFileWebPath(httpd_req_t *req, char* path) {    
    ESP_LOGI(TAG, "Opening file path %s", path);
    
    FILE* f = fopen(path, "r");    
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for reading", req->uri);
        return ESP_FAIL;
    }

    char *buffer = malloc(BUF_SIZE);    
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, path);
    size_t chunksize;
    do {
        /* Read file in chunks into the scratch buffer */
        //chunksize = fread(buffer, 1, SCRATCH_BUFSIZE, f);
        chunksize = fread(buffer, 1, BUF_SIZE, f);

        if (chunksize > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, buffer, chunksize) != ESP_OK) {
                fclose(f);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                free(buffer);
                return ESP_FAIL;
            }
        }

        /* Keep looping till the whole file is sent */
    } while (chunksize != 0);
    
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    free(buffer);
    return ESP_OK;
}

esp_err_t getFileWeb(httpd_req_t *req) {    
    char path[PATH_SIZE];
    strcpy(path, getWWWroot());    
    if (req->uri[strlen(req->uri) - 1] == '/') 
        strcat(path, "/index.html");
    else   
        strcat(path, req->uri);    
    return getFileWebPath(req, path);
}    

esp_err_t getLogFile(httpd_req_t *req) {    
    return getFileWebPath(req, logPath);
}

// esp_err_t getFileWeb(httpd_req_t *req) {
//     char path[PATH_SIZE];
//     strcpy(path, getWWWroot());    
//     if (req->uri[strlen(req->uri) - 1] == '/') 
//         strcat(path, "/index.html");
//     else   
//     	strcat(path, req->uri);    
//     ESP_LOGI(TAG, "Opening file path %s", path);
    
//     FILE* f = fopen(path, "r");    
//     if (f == NULL) {
//         ESP_LOGE(TAG, "Failed to open file %s for reading", req->uri);
//         return ESP_FAIL;
//     }

//     char *buffer = malloc(BUF_SIZE);    
//     if (buffer == NULL) {
//         ESP_LOGE(TAG, "Failed to allocate buffer");
//         return ESP_FAIL;
//     }

//     set_content_type_from_file(req, path);
//     size_t chunksize;
//     do {
//         /* Read file in chunks into the scratch buffer */
//         //chunksize = fread(buffer, 1, SCRATCH_BUFSIZE, f);
//         chunksize = fread(buffer, 1, BUF_SIZE, f);

//         if (chunksize > 0) {
//             /* Send the buffer contents as HTTP response chunk */
//             if (httpd_resp_send_chunk(req, buffer, chunksize) != ESP_OK) {
//                 fclose(f);
//                 ESP_LOGE(TAG, "File sending failed!");
//                 /* Abort sending file */
//                 httpd_resp_sendstr_chunk(req, NULL);
//                 /* Respond with 500 Internal Server Error */
//                 httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
//                 free(buffer);
//                 return ESP_FAIL;
//             }
//         }

//         /* Keep looping till the whole file is sent */
//     } while (chunksize != 0);
    
//     fclose(f);
//     httpd_resp_send_chunk(req, NULL, 0);
//     free(buffer);
//     return ESP_OK;
// }

esp_err_t setFileWeb(httpd_req_t *req) {
    char filepath[PATH_SIZE];
    FILE *fd = NULL;
    struct stat file_stat;
    strcpy(filepath, req->uri + sizeof("/service/upload"));
    
    /* Filename cannot have a trailing '/' */
    if (filepath[strlen(filepath) - 1] == '/') {
        ESP_LOGE(TAG, "Invalid filepath : %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filepath");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        ESP_LOGE(TAG, "File already exists : %s", filepath);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File already exists");
        return ESP_FAIL;
    }

    /* File cannot be larger than a limit */
    if (req->content_len > MAX_FILE_SIZE) {
        ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
        /* Respond with 400 Bad Request */
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "File size must be less than "
                            MAX_FILE_SIZE_STR "!");
        /* Return failure to close underlying connection else the
         * incoming file content will keep the socket busy */
        return ESP_FAIL;
    }

    char fileFullPath[PATH_SIZE];    
    strcpy(fileFullPath, getWWWroot()); 
    //strcpy(fileFullPath, sdmount);
    strcat(fileFullPath, "/");
    strcat(fileFullPath, filepath);

    fd = fopen(fileFullPath, "w");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to create file : %s", fileFullPath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    /* Retrieve the pointer to scratch buffer for temporary storage */
    //char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    char *buf = malloc(BUF_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Cannot malloc buf!");
        return ESP_FAIL;
    }
    int received;

    /* Content length of the request gives
     * the size of the file being uploaded */
    int remaining = req->content_len;

    while (remaining > 0) {

        ESP_LOGI(TAG, "Remaining size : %d", remaining);
        /* Receive the file part by part into a buffer */
        if ((received = httpd_req_recv(req, buf, MIN(remaining, BUF_SIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry if timeout occurred */
                continue;
            }

            /* In case of unrecoverable error,
             * close and delete the unfinished file*/
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File reception failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
            free(buf);
            return ESP_FAIL;
        }

        /* Write buffer content to file on storage */
        if (received && (received != fwrite(buf, 1, received, fd))) {
            /* Couldn't write everything to file!
             * Storage may be full? */
            fclose(fd);
            unlink(filepath);

            ESP_LOGE(TAG, "File write failed!");
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file to storage");
            free(buf);
            return ESP_FAIL;
        }

        /* Keep track of remaining size of
         * the file left to be uploaded */
        remaining -= received;
    }

    /* Close file upon upload completion */
    fclose(fd);
    ESP_LOGI(TAG, "File reception complete");

    /* Redirect onto root to see the updated file list */
    // httpd_resp_set_status(req, "303 See Other");
    // httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    free(buf);
    return ESP_OK;
}

esp_err_t writeLog(char* type, char* buffer) {
    // write logs
    FILE *fd = fopen(logPath, "a+");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file %s", logPath);        
        return ESP_FAIL;
    }

    char buf[10];
    char *time = getCurrentDateTime("%Y-%m-%d %H:%M:%S");    
    fwrite(time, 1, strlen(time), fd);
    free(time);
    fwrite(";", 1, 1, fd);
    unsigned long ticks = esp_timer_get_time() / 1000;
    itoa(ticks, buf, 10);
    fwrite(buf, 1, strlen(buf), fd);

    fwrite(";", 1, 1, fd);
    fwrite(type, 1, strlen(type), fd);
    fwrite(";", 1, 1, fd);

    int bufSize = strlen(buffer);    
    if (bufSize != fwrite(buffer, 1, bufSize, fd)) {
        /* Couldn't write everything to file!
         * Storage may be full? */
        fclose(fd);
        unlink(logPath);
        ESP_LOGE(TAG, "File write failed!");
        return ESP_FAIL;
    }
    fwrite("\n", 1, 1, fd);    
    fclose(fd);
    ESP_LOGD(TAG, "File saved successfully");
    return ESP_OK;
}