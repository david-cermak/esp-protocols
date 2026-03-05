/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include "mosq_broker.h"

extern "C" {
    int db__messages_easy_queue(void *context, const char *topic, uint8_t qos,
                                uint32_t payloadlen, const void *payload,
                                int retain, uint32_t message_expiry_interval,
                                void **properties);
}

namespace {

struct broker_scope {
    std::thread thread;
    int rc = -1;

    broker_scope(const char *host, int port, mosq_message_cb_t msg_cb = nullptr)
    {
        config.host = host;
        config.port = port;
        config.handle_message_cb = msg_cb;
        thread = std::thread([this]() {
            rc = mosq_broker_run(&config);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    ~broker_scope()
    {
        mosq_broker_stop();
        thread.join();
    }

    operator int() const
    {
        return rc;
    }

private:
    struct mosq_broker_config config = {};
};

/**
 * Minimal raw-socket MQTT 3.1.1 client for testing purposes.
 * Supports only CONNECT, SUBSCRIBE and basic packet reading.
 */
class raw_mqtt_client {
public:
    raw_mqtt_client(const char *host, int port, const char *client_id)
        : client_id_(client_id)
    {
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(sock_ >= 0);
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);
        REQUIRE(connect(sock_, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    }

    ~raw_mqtt_client()
    {
        if (sock_ >= 0) {
            close(sock_);
        }
    }

    bool mqtt_connect()
    {
        uint8_t id_len = strlen(client_id_);
        uint8_t remaining = 10 + 2 + id_len;

        uint8_t pkt[256] = {};
        int pos = 0;
        pkt[pos++] = 0x10; // CONNECT
        pkt[pos++] = remaining;
        // Protocol name "MQTT"
        pkt[pos++] = 0x00; pkt[pos++] = 0x04;
        pkt[pos++] = 'M'; pkt[pos++] = 'Q'; pkt[pos++] = 'T'; pkt[pos++] = 'T';
        pkt[pos++] = 0x04; // Protocol level (3.1.1)
        pkt[pos++] = 0x02; // Clean session
        pkt[pos++] = 0x00; pkt[pos++] = 0x3C; // Keep alive 60s
        pkt[pos++] = 0x00; pkt[pos++] = id_len;
        memcpy(&pkt[pos], client_id_, id_len);
        pos += id_len;

        if (send(sock_, pkt, pos, 0) != pos) {
            return false;
        }

        uint8_t resp[4];
        int n = recv(sock_, resp, sizeof(resp), 0);
        return n == 4 && resp[0] == 0x20 && resp[3] == 0x00; // CONNACK success
    }

    bool mqtt_subscribe(const char *topic)
    {
        uint8_t topic_len = strlen(topic);
        uint8_t remaining = 2 + 2 + topic_len + 1;

        uint8_t pkt[256] = {};
        int pos = 0;
        pkt[pos++] = 0x82; // SUBSCRIBE
        pkt[pos++] = remaining;
        pkt[pos++] = 0x00; pkt[pos++] = 0x01; // Packet ID = 1
        pkt[pos++] = 0x00; pkt[pos++] = topic_len;
        memcpy(&pkt[pos], topic, topic_len);
        pos += topic_len;
        pkt[pos++] = 0x00; // QoS 0

        if (send(sock_, pkt, pos, 0) != pos) {
            return false;
        }

        uint8_t resp[5];
        int n = recv(sock_, resp, sizeof(resp), 0);
        return n == 5 && resp[0] == 0x90; // SUBACK
    }

    int drain(int timeout_ms)
    {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        uint8_t buf[1024];
        int total = 0;
        while (true) {
            int n = recv(sock_, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            total += n;
        }
        return total;
    }

private:
    int sock_ = -1;
    const char *client_id_;
};

} // namespace

TEST_CASE("Start and stop mosquitto broker", "[mosquitto]")
{
    broker_scope broker("0.0.0.0", 18833);
    CHECK(broker.rc == -1); // still running
}

TEST_CASE("Restart mosquitto broker after stop", "[mosquitto]")
{
    {
        broker_scope broker("0.0.0.0", 18834);
    }
    {
        broker_scope broker("0.0.0.0", 18834);
    }
}

TEST_CASE("Publish from external thread while client is connected", "[mosquitto][publish]")
{
    constexpr int PORT = 18835;
    constexpr int NUM_MESSAGES = 100;
    constexpr const char *TOPIC = "test/race";

    broker_scope broker("0.0.0.0", PORT);

    raw_mqtt_client client("127.0.0.1", PORT, "test-sub");
    REQUIRE(client.mqtt_connect());
    REQUIRE(client.mqtt_subscribe(TOPIC));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::atomic<int> published{0};
    std::thread pub_thread([&]() {
        char payload[64];
        for (int i = 0; i < NUM_MESSAGES; i++) {
            int len = snprintf(payload, sizeof(payload), "msg-%d", i);
            db__messages_easy_queue(nullptr, TOPIC, 0, len, payload, 0, 0, nullptr);
            published++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    pub_thread.join();
    CHECK(published == NUM_MESSAGES);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int bytes = client.drain(500);
    CHECK(bytes > 0);
}

TEST_CASE("Publish while client disconnects", "[mosquitto][publish][race]")
{
    constexpr int PORT = 18836;
    constexpr const char *TOPIC = "test/disconnect-race";

    broker_scope broker("0.0.0.0", PORT);

    std::atomic<bool> stop{false};

    std::thread pub_thread([&]() {
        char payload[64];
        int i = 0;
        while (!stop) {
            int len = snprintf(payload, sizeof(payload), "msg-%d", i++);
            db__messages_easy_queue(nullptr, TOPIC, 0, len, payload, 0, 0, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    for (int round = 0; round < 5; round++) {
        auto *client = new raw_mqtt_client("127.0.0.1", PORT, "test-disco");
        REQUIRE(client->mqtt_connect());
        REQUIRE(client->mqtt_subscribe(TOPIC));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        delete client; // abrupt disconnect while publisher is active
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stop = true;
    pub_thread.join();
}

extern "C" void app_main(void)
{
    int result = Catch::Session().run();
    if (result != 0) {
        printf("Test failed with result %d.\n", result);
    } else {
        printf("All tests passed successfully.\n");
    }
    std::exit(result);
}
