/* PPPoS Client Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "mqtt_client.h"
#include "esp_modem_config.h"
#include "cxx_include/esp_modem_api.hpp"
#include <cxx_include/esp_modem_dce_factory.hpp>
#include "esp_log.h"

#define BROKER_URL "mqtt://mqtt.eclipseprojects.io"

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

class DCE: public esp_modem::GenericModule {
    using esp_modem::GenericModule::GenericModule;
public:
    esp_modem::command_result net_open() { return esp_modem::dce_commands::net_open(dte.get()); }
    esp_modem::command_result net_close() { return esp_modem::dce_commands::net_close(dte.get()); }
    esp_modem::command_result tcp_open(const std::string& host, int port, int timeout) { return esp_modem::dce_commands::tcp_open(dte.get(), host, port, timeout); }
    esp_modem::command_result tcp_close() { return esp_modem::dce_commands::tcp_close(dte.get()); }
    esp_modem::command_result tcp_send(uint8_t *data, size_t len) { return esp_modem::dce_commands::tcp_send(dte.get(), data, len); }
    esp_modem::command_result tcp_recv(uint8_t *data, size_t len) { return esp_modem::dce_commands::tcp_recv(dte.get(), data, len); }


};

class LocalFactory: public esp_modem::dce_factory::Factory {
public:
    static std::unique_ptr<DCE> create(const esp_modem::dce_config *config, std::shared_ptr<esp_modem::DTE> dte)
    {
        return esp_modem::dce_factory::Factory::build_module_T<DCE, std::unique_ptr<DCE>>(config, std::move(dte));
    }
};


//using namespace esp_modem;

#define CHECK(cmd) if (cmd != esp_modem::command_result::OK) {  \
                    ESP_LOGE(TAG, #cmd);                        \
                    ESP_LOGE(TAG, "FAILED");                    \
                    return;                                     \
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
    dte_config.uart_config.tx_io_num = CONFIG_EXAMPLE_MODEM_UART_TX_PIN;
    dte_config.uart_config.rx_io_num = CONFIG_EXAMPLE_MODEM_UART_RX_PIN;
    dte_config.uart_config.rts_io_num = CONFIG_EXAMPLE_MODEM_UART_RTS_PIN;
    dte_config.uart_config.cts_io_num = CONFIG_EXAMPLE_MODEM_UART_CTS_PIN;
    dte_config.uart_config.rx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE;
    dte_config.uart_config.tx_buffer_size = CONFIG_EXAMPLE_MODEM_UART_TX_BUFFER_SIZE;
    dte_config.uart_config.event_queue_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_QUEUE_SIZE;
    dte_config.task_stack_size = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_STACK_SIZE*2;
    dte_config.task_priority = CONFIG_EXAMPLE_MODEM_UART_EVENT_TASK_PRIORITY;
    dte_config.dte_buffer_size = CONFIG_EXAMPLE_MODEM_UART_RX_BUFFER_SIZE / 2;

    auto dte = esp_modem::create_uart_dte(&dte_config);
    assert(dte);

    /* Configure the DCE */

    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_EXAMPLE_MODEM_PPP_APN);
    auto dce = LocalFactory::create(&dce_config, std::move(dte));
    dce->setup_data_mode();
    vTaskDelay(pdMS_TO_TICKS(1000));
    (dce->net_close());
    vTaskDelay(pdMS_TO_TICKS(1000));
    (dce->net_open());
    vTaskDelay(pdMS_TO_TICKS(1000));
    (dce->tcp_close());
    vTaskDelay(pdMS_TO_TICKS(1000));


    for (int i=0; i<5; ++i) {
        std::string out;
        auto ret = dce->at("AT", out);
        if (ret != esp_modem::command_result::OK) {
            ESP_LOGE(TAG, "at failed with %d", ret);
        }
        ESP_LOGI(TAG, "%s\n", out.c_str());
        ESP_LOGI(TAG, "ok");
    }

    std::string resp;

    auto ret = dce->at("AT+IPADDR", resp);
    while (ret != esp_modem::command_result::OK) {
        ret = dce->at("AT+IPADDR", resp);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    CHECK(dce->tcp_open("www.seznam.cz", 80, 50000));
    vTaskDelay(pdMS_TO_TICKS(1000));
    std::vector<uint8_t> data(100);
    std::string test = "test";
    CHECK(dce->tcp_send((uint8_t *) test.c_str(), test.size()));
    vTaskDelay(pdMS_TO_TICKS(1000));
    CHECK(dce->tcp_recv((uint8_t *) test.c_str(), test.size()));
    return;


    while (1) {
        std::string out;
        auto ret = dce->at("AT", out);
        if (ret != esp_modem::command_result::OK) {
            ESP_LOGE(TAG, "get_signal_quality failed with %d", ret);
            return;
        }
        ESP_LOGI(TAG, "%s\n", out.c_str());
        ESP_LOGI(TAG, "ok");
    }
}
