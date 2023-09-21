/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cxx_include/esp_modem_dte.hpp"
#include "esp_modem_config.h"
#include "cxx_include/esp_modem_api.hpp"
#include "simple_mqtt_client.hpp"
#include "esp_vfs_dev.h"        // For optional VFS support
#include "esp_https_ota.h"
#include "vfs_resource/vfs_create.hpp"
#include <esp_netif_ppp.h>
#include "network_dce.hpp"

using namespace esp_modem;

static const char *TAG = "ota_test";

class StatusHandler {
public:
    static constexpr auto IP_Event      = SignalGroup::bit0;
    static constexpr auto MQTT_Connect  = SignalGroup::bit1;
    static constexpr auto MQTT_Data     = SignalGroup::bit2;

    StatusHandler()
    {
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event, this));
    }

    ~StatusHandler()
    {
        esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID, on_event);
    }

    void handle_mqtt(MqttClient *client)
    {
        mqtt_client = client;
        client->register_handler(ESP_EVENT_ANY_ID, on_event, this);
    }

    esp_err_t wait_for(decltype(IP_Event) event, int milliseconds)
    {
        return signal.wait_any(event, milliseconds);
    }

    ip_event_t get_ip_event_type()
    {
        return ip_event_type;
    }

private:
    static void on_event(void *arg, esp_event_base_t base, int32_t event, void *data)
    {
        auto *handler = static_cast<StatusHandler *>(arg);
        if (base == IP_EVENT) {
            handler->ip_event(event, data);
        } else {
            handler->mqtt_event(event, data);
        }
    }

    void ip_event(int32_t id, void *data)
    {
        if (id == IP_EVENT_PPP_GOT_IP) {
            auto *event = (ip_event_got_ip_t *)data;
            ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
            ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
            signal.set(IP_Event);
        } else if (id == IP_EVENT_PPP_LOST_IP) {
            signal.set(IP_Event);
        }
        ip_event_type = static_cast<ip_event_t>(id);
    }

    void mqtt_event(int32_t event, void *data)
    {
        if (mqtt_client && event == MqttClient::get_event(MqttClient::Event::CONNECT)) {
            signal.set(MQTT_Connect);
        } else if (mqtt_client && event == mqtt_client->get_event(MqttClient::Event::DATA)) {
            ESP_LOGI(TAG, " TOPIC: %s", mqtt_client->get_topic(data).c_str());
            ESP_LOGI(TAG, " DATA: %s", mqtt_client->get_data(data).c_str());
            signal.set(MQTT_Data);
        }
    }

    esp_modem::SignalGroup signal{};
    MqttClient *mqtt_client{nullptr};
    ip_event_t ip_event_type{};
};

void ota_example_task(void *pvParameter);

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ota_test", ESP_LOG_DEBUG);

    // Initialize system functions
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());

    // Initialize DTE
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
#ifdef CONFIG_TEST_USE_VFS_TERM
    // To code-cover the vfs layers
    struct esp_modem_vfs_uart_creator uart_config = ESP_MODEM_VFS_DEFAULT_UART_CONFIG("/dev/uart/1");
    assert(vfs_create_uart(&uart_config, &dte_config.vfs_config) == true);

    auto dte = create_vfs_dte(&dte_config);
    esp_vfs_dev_uart_use_driver(uart_config.uart.port_num);
#else
    auto dte = create_uart_dte(&dte_config);
#endif // CONFIG_TEST_USE_VFS_TERM
    assert(dte);
    dte->set_error_cb([](terminal_error err) {
        ESP_LOGE(TAG, "DTE reported terminal error: %d", static_cast<int>(err));
    });

    // Initialize PPP netif
    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif);

    // Initialize DCE
#ifdef CONFIG_TEST_DEVICE_PPPD_SERVER
    auto dce = create(dte, esp_netif);
#else
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_TEST_MODEM_APN);
    auto dce = create_generic_dce(&dce_config, dte, esp_netif);
    assert(dce);
#endif


    /* Try to connect to the network and publish an mqtt topic */
    StatusHandler handler;

    {
        esp_netif_ppp_config_t cfg;
        ESP_ERROR_CHECK(esp_netif_ppp_get_params(esp_netif, &cfg));
        cfg.ppp_lcp_echo_disabled = true;
        ESP_ERROR_CHECK(esp_netif_ppp_set_params(esp_netif, &cfg));
    }

#ifndef CONFIG_TEST_DEVICE_PPPD_SERVER
    if (dce->set_mode(esp_modem::modem_mode::CMUX_MANUAL_MODE) &&
            dce->set_mode(esp_modem::modem_mode::CMUX_MANUAL_SWAP) &&
            dce->set_mode(esp_modem::modem_mode::CMUX_MANUAL_DATA)) {
#else
    if (dce->set_mode(esp_modem::modem_mode::DATA_MODE)) {
//    if (dce->set_mode(esp_modem::modem_mode::CMUX_MODE)) {
#endif
        ESP_LOGI(TAG, "Modem has correctly entered the desired mode (CMUX/DATA/Manual CMUX)");
    } else {
        ESP_LOGE(TAG, "Failed to configure multiplexed command mode... exiting");
        return;
    }

    if (!handler.wait_for(StatusHandler::IP_Event, 60000)) {
        ESP_LOGE(TAG, "Cannot get IP within specified timeout... exiting");
        return;
    } else if (handler.get_ip_event_type() == IP_EVENT_PPP_GOT_IP) {
        ESP_LOGI(TAG, "Got IP address");

        /* When connected to network, subscribe and publish some MQTT data */
        MqttClient mqtt(CONFIG_BROKER_URI);
        handler.handle_mqtt(&mqtt);
        mqtt.connect();
        if (!handler.wait_for(StatusHandler::MQTT_Connect, 60000)) {
            ESP_LOGE(TAG, "Cannot connect to %s within specified timeout... exiting", CONFIG_BROKER_URI);
            return;
        }
        ESP_LOGI(TAG, "Connected");

        mqtt.subscribe("/topic/esp-modem");
        mqtt.publish("/topic/esp-modem", "Hello modem");
        if (!handler.wait_for(StatusHandler::MQTT_Data, 60000)) {
            ESP_LOGE(TAG, "Didn't receive published data within specified timeout... exiting");
            return;
        }
        ESP_LOGI(TAG, "Received MQTT data");

    } else if (handler.get_ip_event_type() == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGE(TAG, "PPP client has lost connection... exiting");
        return;
    }

    esp_http_client_config_t config = { };
    config.skip_cert_common_name_check = true;
    config.url = CONFIG_TEST_OTA_URI;
    config.keep_alive_enable = true;
    config.timeout_ms = 30000;
    config.save_client_session = true;

    xTaskCreate(&ota_example_task, "ota_example_task", 8192, dce.get(), 5, nullptr);

#ifndef CONFIG_TEST_DEVICE_PPPD_SERVER
    while (true) {
        std::string str;
        if (dce->get_imsi(str) == esp_modem::command_result::OK) {
            ESP_LOGI(TAG, "Modem IMSI number: %s", str.c_str());
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelay(portMAX_DELAY);
#endif // CONFIG_TEST_DEVICE_PPPD_SERVER

    esp_https_ota_config_t ota_config = { };
    ota_config.http_config = &config;

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
        return;
    }

    if (dce->set_mode(esp_modem::modem_mode::CMUX_MANUAL_EXIT)) {
        ESP_LOGI(TAG, "Modem CMUX mode exit");
    } else {
        ESP_LOGE(TAG, "Failed to configure desired mode... exiting");
        return;
    }
}
