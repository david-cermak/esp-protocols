/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* PPPoS Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string>
#include <esp_transport_tcp.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "mqtt_client.h"
#include "esp_modem_config.h"
#include "cxx_include/esp_modem_api.hpp"
#include "sock_dce.hpp"
#include "esp_log.h"
#include "esp_transport_tcp.h"

#define BROKER_URL "mqtt.eclipseprojects.io"

esp_transport_handle_t esp_transport_tls_init(esp_transport_handle_t parent);

static const char *TAG = "modem_client";
static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;
static const int GOT_DATA_BIT = BIT2;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/esp-pppos", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/esp-pppos", "esp32-pppos", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        xEventGroupSetBits(event_group, GOT_DATA_BIT);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "MQTT other event id: %d", event->event_id);
        break;
    }
}

static int tcp_connect(esp_transport_handle_t t, const char *host, int port, int timeout_ms)
{
    ESP_LOGI(TAG, "host=%s", host);
    auto *dce = (sock_dce::DCE *)esp_transport_get_context_data(t);
//    if (dce->tcp_open(host, port, timeout_ms) == esp_modem::command_result::OK) {
//        return 1;
//    }
//    return -1;
    if (!dce->start(host, port)) {
        return -1;
    }
    ESP_LOGI(TAG, "Waiting for connection...");
    if (dce->wait_to_idle(timeout_ms)) {
        dce->set_idle();
        return 1;
    }
    return -1;
}

static int tcp_read(esp_transport_handle_t t, char *buffer, int len, int timeout_ms)
{
    auto *dce = (sock_dce::DCE *)esp_transport_get_context_data(t);
    auto ret = dce->wait_to_read(0);
    if (ret > 0) {
        return dce->sync_recv(buffer, len, timeout_ms);
    }
    return ret;

}

static int tcp_write(esp_transport_handle_t t, const char *buffer, int len, int timeout_ms)
{

    auto *dce = (sock_dce::DCE *)esp_transport_get_context_data(t);
    return dce->sync_send(buffer, len, timeout_ms);

}

static int base_close(esp_transport_handle_t t)
{
    auto *dce = (sock_dce::DCE *)esp_transport_get_context_data(t);
    return dce->tcp_close() == esp_modem::command_result::OK ? 0 : -1;
}

static int base_poll_read(esp_transport_handle_t t, int timeout_ms)
{
    auto *dce = (sock_dce::DCE *)esp_transport_get_context_data(t);
    return dce->wait_to_read(timeout_ms);
}

static int base_poll_write(esp_transport_handle_t t, int timeout_ms)
{
//    auto *dce = (sock_dce::DCE *)esp_transport_get_context_data(t);
//    if (!dce->wait_to_idle(timeout_ms)) {
//        return 0;
//    }
    return 1;
}

static int base_destroy(esp_transport_handle_t transport)
{
    return 0;
}

extern "C" void app_main(void)
{

    /* Init and register system/core components */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    event_group = xEventGroupCreate();

    /* Configure and create the UART DTE */
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    /* setup UART specific configuration based on kconfig options */
    dte_config.uart_config.tx_io_num = 25;
    dte_config.uart_config.rx_io_num = 26;
    dte_config.uart_config.rts_io_num = CONFIG_EXAMPLE_MODEM_UART_RTS_PIN;
    dte_config.uart_config.cts_io_num = CONFIG_EXAMPLE_MODEM_UART_CTS_PIN;
    dte_config.uart_config.rx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE;
    dte_config.uart_config.tx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_TX_BUFFER_SIZE;
    dte_config.uart_config.event_queue_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_QUEUE_SIZE;
    dte_config.task_stack_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_STACK_SIZE * 2;
    dte_config.task_priority = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_PRIORITY;
    dte_config.dte_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE / 2;

    auto dte = esp_modem::create_uart_dte(&dte_config);
    assert(dte);

    /* Configure the DCE */
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_EXAMPLE_MODEM_APN);

    /* create the DCE and initialize network manually (using AT commands) */
    auto dce = sock_dce::create(&dce_config, std::move(dte));
    if (!dce->init_network()) {
        ESP_LOGE(TAG,  "Failed to setup network");
        return;
    }

    dce->init_sock(8883);
    esp_mqtt_client_config_t mqtt_config = {};
    esp_transport_handle_t at = esp_transport_init();
    esp_transport_set_context_data(at, (void *)dce.get());
    esp_transport_set_func(at, tcp_connect, tcp_read, tcp_write, base_close, base_poll_read, base_poll_write, base_destroy);
    esp_transport_handle_t ssl = esp_transport_tls_init(at);
    ESP_LOGE(TAG, "at handle=%p", at);
    ESP_LOGE(TAG, "ssl handle=%p", ssl);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
//    mqtt_config.broker.address.uri = "mqtt://test.mosquitto.org";
    mqtt_config.broker.address.uri = "mqtt://mqtt.eclipseprojects.io";
    mqtt_config.broker.address.port = 8883;
    mqtt_config.session.message_retransmit_timeout = 10000;
    mqtt_config.network.transport = ssl;
#else
    mqtt_config.uri = "mqtt://127.0.0.1";
    mqtt_config.message_retransmit_timeout = 10000;
#endif
    esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(mqtt_client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
//    if (!dce->start(BROKER_URL, 1883)) {
//        ESP_LOGE(TAG, "Failed to start DCE");
//        return;
//    }
    while (1) {
//        while (dce->perform_sock()) {
//            ESP_LOGV(TAG, "...performing");
//        }
//        ESP_LOGE(TAG, "Loop exit.. retrying");
        vTaskDelay(pdMS_TO_TICKS(5000));
        // handle disconnections errors
//        if (!dce->init_network()) {
//            ESP_LOGE(TAG, "Failed to reinit network");
//            return;
//        }
//        if (!dce->start("test.mosquitto.org", 1883)) {
//            ESP_LOGI(TAG, "Network reinitialized, retrying");
//        }
    }

}
