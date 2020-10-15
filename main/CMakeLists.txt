idf_component_register(SRCS "main.c"
                            "utils.c"
                            "storage.c"
                            "sdcard.c"
                            "spiffs.c"
                            "core.c"
                            "network.c"
                            "webServer.c"
                            "modbus.c"
                            "ota.c"
                            "ftp.c"
                       INCLUDE_DIRS "."
                       EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)

