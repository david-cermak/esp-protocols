/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "eppp_link.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "ping/ping_sock.h"
#include "esp_console.h"
#include "esp_wifi_remote.h"

void register_iperf(void);
esp_err_t client_init(void);

static const char *TAG = "eppp_host_example";

#if CONFIG_EXAMPLE_MQTT
static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
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
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_EXAMPLE_BROKER_URL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
#endif // MQTT

#if CONFIG_EXAMPLE_ICMP_PING
static void test_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    printf("%" PRId32 "bytes from %s icmp_seq=%d ttl=%d time=%" PRId32 " ms\n",
           recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
}

static void test_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    printf("From %s icmp_seq=%d timeout\n", inet_ntoa(target_addr.u_addr.ip4), seqno);
}

static void test_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    printf("%" PRId32 " packets transmitted, %" PRId32 " received, time %" PRId32 "ms\n", transmitted, received, total_time_ms);

}
#endif // PING

/*
 * local netifs (wifi and ppp)
 */
static esp_netif_t *s_wifi_netif;
static esp_netif_t *s_ppp_netif;
static eppp_channel_fn_t s_tx;

static esp_err_t remote_wifi_receive(void *h, void *buffer, size_t len)
{
    if (s_wifi_netif) {
        return esp_netif_receive(s_wifi_netif, buffer, len, NULL);
    }
    return ESP_OK;
}

esp_err_t remote_wifi_transmit_wrap(void *h, void *buffer, size_t len, void *netstack_buffer)
{
    if (s_tx) {
        return s_tx(s_ppp_netif, buffer, len);
    }
    return ESP_OK;
}

static esp_err_t remote_wifi_transmit(void *h, void *buffer, size_t len)
{
    if (s_tx) {
        return s_tx(s_ppp_netif, buffer, len);
    }
    return ESP_OK;
}

// this is needed as the ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA config frees the eb on pbuf-free
static void wifi_free(void *h, void *buffer)
{
}

static void remote_wifi_netif(void)
{
    esp_netif_driver_ifconfig_t driver_cfg = {
        .handle = (void *)1,
        .transmit = remote_wifi_transmit,
        .transmit_wrap = remote_wifi_transmit_wrap,
        .driver_free_rx_buffer = wifi_free

    };
    const esp_netif_driver_ifconfig_t *wifi_driver_cfg = &driver_cfg;
    esp_netif_config_t netif_config = {
        .base = ESP_NETIF_BASE_DEFAULT_WIFI_STA,
        .driver = wifi_driver_cfg,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA
    };
    s_wifi_netif = esp_netif_new(&netif_config);
}

static void wifi_init(void *ctx)
{
    client_init();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_remote_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };

    esp_err_t err = esp_wifi_remote_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "esp_wifi_remote_set_mode() returned = %x", err);
    ESP_ERROR_CHECK(esp_wifi_remote_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_remote_start() );
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_remote_get_mac(WIFI_IF_STA, mac) );

    esp_netif_set_mac(s_wifi_netif, mac);
    vTaskDelay(pdMS_TO_TICKS(1000));    // we're not supporting WIFI_EVENT yet, just play with delays for now

    esp_netif_action_start(s_wifi_netif, 0, 0, 0);
    ESP_ERROR_CHECK(esp_wifi_remote_connect() );

    esp_netif_action_connected(s_wifi_netif, 0, 0, 0);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Sets up the default EPPP-connection
     */
    eppp_config_t config = EPPP_DEFAULT_CLIENT_CONFIG();
#if CONFIG_EPPP_LINK_DEVICE_SPI
    config.transport = EPPP_TRANSPORT_SPI;
    config.task.priority = 14;
    config.spi.freq = 40000000;
#else
    config.transport = EPPP_TRANSPORT_UART;
    config.uart.tx_io = 10;
    config.uart.rx_io = 11;
    config.uart.baud = 2000000;
#endif
    s_ppp_netif = eppp_connect(&config);
    if (s_ppp_netif == NULL) {
        ESP_LOGE(TAG, "Failed to connect");
        return ;
    }
    eppp_add_channel(1, &s_tx, remote_wifi_receive);
    remote_wifi_netif();
    if (s_wifi_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create wifi netif");
        return ;
    }

    xTaskCreate(&wifi_init, "initwifi", 8192, NULL, 18, NULL);

#if CONFIG_EXAMPLE_IPERF
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    repl_config.prompt = "iperf>";
    // init console REPL environment
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    register_iperf();

    printf("\n =======================================================\n");
    printf(" |       Steps to Test PPP Client Bandwidth            |\n");
    printf(" |                                                     |\n");
    printf(" |  1. Enter 'help', check all supported commands      |\n");
    printf(" |  2. Start PPP server on host system                 |\n");
    printf(" |     - pppd /dev/ttyUSB1 115200 192.168.11.1:192.168.11.2 modem local noauth debug nocrtscts nodetach +ipv6\n");
    printf(" |  3. Wait ESP32 to get IP from PPP server            |\n");
    printf(" |  4. Enter 'pppd info' (optional)                    |\n");
    printf(" |  5. Server: 'iperf -u -s -i 3'                      |\n");
    printf(" |  6. Client: 'iperf -u -c SERVER_IP -t 60 -i 3'      |\n");
    printf(" |                                                     |\n");
    printf(" =======================================================\n\n");

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
#endif

#if CONFIG_EXAMPLE_ICMP_PING
    ip_addr_t target_addr = { .type = IPADDR_TYPE_V4, .u_addr.ip4.addr = esp_netif_htonl(CONFIG_EXAMPLE_PING_ADDR) };

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.timeout_ms = 2000;
    ping_config.interval_ms = 20,
    ping_config.target_addr = target_addr;
    ping_config.count = 100; // ping in infinite mode
    /* set callback functions */
    esp_ping_callbacks_t cbs;
    cbs.on_ping_success = test_on_ping_success;
    cbs.on_ping_timeout = test_on_ping_timeout;
    cbs.on_ping_end = test_on_ping_end;
    esp_ping_handle_t ping;
    esp_ping_new_session(&ping_config, &cbs, &ping);
    /* start ping */
    esp_ping_start(ping);
#endif // PING

#if CONFIG_EXAMPLE_MQTT
    mqtt_app_start();
#endif
}
