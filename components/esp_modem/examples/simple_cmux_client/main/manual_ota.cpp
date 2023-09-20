/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "errno.h"
#include <esp_transport_tcp.h>
#include "cxx_include/esp_modem_api.hpp"

esp_transport_handle_t esp_transport_batch_tls_init(esp_transport_handle_t parent);
bool esp_transport_batch_tls_pre_read(esp_transport_handle_t t, int len, int timeout_ms);


#define OTA_URL_SIZE 256
#define BUFFSIZE (64*1024)
#define HASH_LEN 32 /* SHA-256 digest length */
static char ota_write_data[BUFFSIZE + 1] = { 0 };

static const char *TAG = "native_ota_example";

static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static void __attribute__((noreturn)) task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    (void)vTaskDelete(NULL);

    while (1) {
        ;
    }
}

static void infinite_loop(void)
{
    int i = 0;
    ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
    while (1) {
        ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}


const int max_http_request_size = BUFFSIZE;

void ota_example_task(void *pvParameter)
{
    esp_modem::DCE *dce = (esp_modem::DCE *)pvParameter;
    esp_transport_handle_t tcp = esp_transport_tcp_init();
    esp_transport_handle_t ssl = esp_transport_batch_tls_init(tcp);
//    esp_log_level_set("batch-tls", ESP_LOG_VERBOSE);
    esp_http_client_config_t config = { };
    config.skip_cert_common_name_check = true;
    config.url = CONFIG_EXAMPLE_PERFORM_OTA_URI;
    config.keep_alive_enable = true;
//    config.timeout_ms = 30000;
//    config.save_client_session = true;
    config.transport = ssl;

//    auto config = static_cast<esp_http_client_config_t *>(pvParameter);
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA example task");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08" PRIx32 ", but running from offset 0x%08" PRIx32,
                 configured->address, running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08" PRIx32 ")",
             running->type, running->subtype, running->address);

//    esp_http_client_config_t config = {
//            .url = "https://192.168.11.1:1234/blink.bin",
////        .cert_pem = (char *)server_cert_pem_start,
////            .timeout_ms = CONFIG_EXAMPLE_OTA_RECV_TIMEOUT,
//            .keep_alive_enable = true,
//            .skip_cert_common_name_check = true,
//            .save_client_session = true
//    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        task_fatal_error();
    }
    esp_http_client_set_method(client, HTTP_METHOD_HEAD);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != HttpStatus_Ok) {
            ESP_LOGE(TAG, "Received incorrect http status %d", status);
            task_fatal_error();
        }
    } else {
        ESP_LOGE(TAG, "ESP HTTP client perform failed: %d", err);
        task_fatal_error();
    }

    int image_length = esp_http_client_get_content_length(client);
    ESP_LOGE(TAG, "image_length = %d", image_length);
    esp_http_client_close(client);

    if (image_length > BUFFSIZE) {
        char *header_val = NULL;
        asprintf(&header_val, "bytes=0-%d", max_http_request_size - 1);
        if (header_val == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for HTTP header");
            task_fatal_error();
        }
        esp_http_client_set_header(client, "Range", header_val);
        free(header_val);
    }
    esp_http_client_set_method(client, HTTP_METHOD_GET);


//    err = esp_http_client_open(client, 0);
//    if (err != ESP_OK) {
//        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
//        esp_http_client_cleanup(client);
//        task_fatal_error();
//    }
//    esp_http_client_fetch_headers(client);
//    vTaskDelay(pdMS_TO_TICKS(20));

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
             update_partition->subtype, update_partition->address);

//    while (1) {
//        int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
//        if (data_read < 0) {
//            ESP_LOGE(TAG, "Error: SSL data read error");
//            http_cleanup(client);
//            task_fatal_error();
//        } else if (data_read > 0) {
////            ESP_LOGE(TAG, "%d", data_read);
//            printf(".");
//            vTaskDelay(pdMS_TO_TICKS(100));
//        } else if (data_read == 0) {
//            if (errno == ECONNRESET || errno == ENOTCONN) {
//                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
//                break;
//            }
//            if (esp_http_client_is_complete_data_received(client) == true) {
//                ESP_LOGI(TAG, "Connection closed");
//                break;
//            }
//        }
//    }
//    ESP_LOGE(TAG, "DONE");
//    task_fatal_error();

    int binary_file_length = 0;
    /*deal with all receive packet*/
    bool image_header_was_checked = false;

    int err_count = 0;
    while (1) {
//        dce->recover();
//        vTaskDelay(pdMS_TO_TICKS(50));
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
//                esp_log_level_set("CMUX Received", ESP_LOG_VERBOSE);
//                esp_log_level_set("Send", ESP_LOG_VERBOSE);
//                esp_log_level_set("CMUX", ESP_LOG_VERBOSE);
//                esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);

//            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            if (err_count++ < 3) {
                client = esp_http_client_init(&config);
                if (client == NULL) {
                    ESP_LOGE(TAG, "Failed to initialise HTTP connection");
                    task_fatal_error();
                }
                esp_http_client_set_method(client, HTTP_METHOD_GET);
                char *header_val = NULL;
                if (image_length == binary_file_length) {
                    break;
                }
                if ((image_length - binary_file_length) > max_http_request_size) {
                    asprintf(&header_val, "bytes=%d-%d", binary_file_length, (binary_file_length + max_http_request_size - 1));
                } else {
                    asprintf(&header_val, "bytes=%d-", binary_file_length);
                    ESP_LOGE(TAG, "Last one value %s", header_val);
                }
                if (header_val == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for HTTP header");
                    task_fatal_error();
                }
                esp_http_client_set_header(client, "Range", header_val);
                free(header_val);
                continue;

            }
            task_fatal_error();
        }
        esp_http_client_fetch_headers(client);

        int batch_len = esp_transport_batch_tls_pre_read(ssl, BUFFSIZE, 2000);
        if (batch_len < 0) {
            ESP_LOGE(TAG, "Error: Failed to preread!");
            http_cleanup(client);
            task_fatal_error();

        }


        int data_read = esp_http_client_read(client, ota_write_data, batch_len);
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            http_cleanup(client);
            task_fatal_error();
        } else if (data_read > 0) {

            esp_http_client_close(client);

            if (image_header_was_checked == false) {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
                            ESP_LOGW(TAG, "New version is the same as invalid version.");
                            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                            http_cleanup(client);
                            infinite_loop();
                        }
                    }
#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
                    if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
                        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
                        http_cleanup(client);
                        infinite_loop();
                    }
#endif

                    image_header_was_checked = true;
//                    vTaskDelay(pdMS_TO_TICKS(500));


                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        http_cleanup(client);
                        esp_ota_abort(update_handle);
                        task_fatal_error();
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");

                } else {
                    ESP_LOGE(TAG, "received package is not fit len");
                    http_cleanup(client);
                    esp_ota_abort(update_handle);
                    task_fatal_error();
                }
            }
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                http_cleanup(client);
                esp_ota_abort(update_handle);
                task_fatal_error();
            }
            binary_file_length += data_read;
            ESP_LOGI(TAG, "Written image length %d", binary_file_length);

            char *header_val = NULL;
            if (image_length == binary_file_length) {
                break;
            }
            if ((image_length - binary_file_length) > max_http_request_size) {
                asprintf(&header_val, "bytes=%d-%d", binary_file_length, (binary_file_length + max_http_request_size - 1));
            } else {
                asprintf(&header_val, "bytes=%d-", binary_file_length);
                ESP_LOGE(TAG, "Last one value %s", header_val);
            }
            if (header_val == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for HTTP header");
                task_fatal_error();
            }
            esp_http_client_set_header(client, "Range", header_val);
            free(header_val);

//            err = esp_http_client_open(client, 0);
//            if (err != ESP_OK) {
//                ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
//                esp_http_client_cleanup(client);
//                task_fatal_error();
//            }
//            esp_http_client_fetch_headers(client);
//            vTaskDelay(pdMS_TO_TICKS(20));

        } else if (data_read == 0) {
            if (binary_file_length == 0) {
                int status_code = esp_http_client_get_status_code(client);
                ESP_LOGW(TAG, "Status code: %d", status_code);
                err = esp_http_client_set_redirection(client);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "URL redirection Failed");
                    task_fatal_error();
                }

                err = esp_http_client_open(client, 0);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
                    esp_http_client_cleanup(client);
                    task_fatal_error();
                }
                esp_http_client_fetch_headers(client);
                continue;

            }

            /*
             * As esp_http_client_read never returns negative error code, we rely on
             * `errno` to check for underlying transport connectivity closure if any
             */
            if (errno == ECONNRESET || errno == ENOTCONN) {
                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                break;
            }
            if (esp_http_client_is_complete_data_received(client) == true) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
        }
    }
    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);
    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE(TAG, "Error in receiving complete file");
        http_cleanup(client);
        esp_ota_abort(update_handle);
        task_fatal_error();
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        http_cleanup(client);
        task_fatal_error();
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        http_cleanup(client);
        task_fatal_error();
    }
    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    return ;
}
