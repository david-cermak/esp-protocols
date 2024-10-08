/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ifaddrs.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_session.hpp"

static char buffer[64];

static esp_netif_t *create_test_netif(const char *if_key)
{
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    esp_netif_ip_info_t ip = { };
    ip.ip.addr = ESP_IP4TOADDR(1, 2, 3, 4);
    base_cfg.ip_info = &ip;
    base_cfg.if_key = if_key;
    esp_netif_config_t cfg = { .base = &base_cfg, .driver = NULL, .stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA };
    return esp_netif_new(&cfg);
}

TEST_CASE("esp_getnameinfo() for IPv4", "[sock_utils]")
{
    struct sockaddr_in sock_addr = {};
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = 257; //  (0x0101: same number for LE and BE)
    REQUIRE(esp_getnameinfo((struct sockaddr *)&sock_addr, sizeof(sock_addr), buffer, sizeof(buffer), NULL, 0, NI_NUMERICHOST) == 0);
    CHECK(strcmp("0.0.0.0", buffer) == 0);
    CHECK(esp_getnameinfo((struct sockaddr *)&sock_addr, sizeof(sock_addr), NULL, 0, buffer, sizeof(buffer), NI_NUMERICSERV) == 0);
    CHECK(strcmp("257", buffer) == 0);
}

TEST_CASE("esp_getnameinfo() for IPv6", "[sock_utils]")
{
    struct sockaddr_in sock_addr = {};
    sock_addr.sin_family = AF_INET6;
    // IPv6 not supported for now
    CHECK(esp_getnameinfo((struct sockaddr *)&sock_addr, sizeof(sock_addr), buffer, sizeof(buffer), NULL, 0, NI_NUMERICHOST) != 0);
}

static void test_getifaddr(int expected_nr_of_addrs)
{
    struct ifaddrs *addresses, *addr;
    int nr_of_addrs = 0;

    CHECK(esp_getifaddrs(&addresses) != -1);
    addr = addresses;

    while (addr) {
        ++nr_of_addrs;
        if (addr->ifa_addr && addr->ifa_addr->sa_family == AF_INET) {   // look for IP4 addresses
            struct sockaddr_in *sock_addr = (struct sockaddr_in *) addr->ifa_addr;
            if (esp_getnameinfo((struct sockaddr *)sock_addr, sizeof(*sock_addr),
                                buffer, sizeof(buffer), NULL, 0, NI_NUMERICHOST) != 0) {
                printf("esp_getnameinfo() failed\n");
            } else {
                printf("IPv4 address of interface \"%s\": %s\n", addr->ifa_name, buffer);
                CHECK(strcmp("1.2.3.4", buffer) == 0);
            }
        }
        addr = addr->ifa_next;
    }
    // check that we got 1 address with exact content
    CHECK(nr_of_addrs == expected_nr_of_addrs);
    esp_freeifaddrs(addresses);
}

TEST_CASE("esp_getifaddrs() with 0, 1, and 2 addresses", "[sock_utils]")
{
    test_getifaddr(0);
    esp_netif_t *esp_netif = create_test_netif("station");
    REQUIRE(esp_netif != NULL);
    test_getifaddr(1);
    esp_netif_t *esp_netif2 = create_test_netif("station2");
    REQUIRE(esp_netif2 != NULL);
    test_getifaddr(2);
    esp_netif_destroy(esp_netif);
    esp_netif_destroy(esp_netif2);
}

static void test_pipe(int read_fd, int write_fd)
{
    CHECK(read_fd  >= 0);
    CHECK(write_fd >= 0);
    CHECK(read(read_fd, buffer, sizeof(buffer)) < 0);
    CHECK(write(write_fd, buffer, 1) > 0);
    CHECK(read(read_fd, buffer, sizeof(buffer)) == 1);

}

TEST_CASE("socketpair()", "[sock_utils]")
{
    int fds[2];
    CHECK(esp_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    printf("socketpair created fds: %d, %d\n", fds[0], fds[1]);
    // check both directions
    test_pipe(fds[0], fds[1]);
    test_pipe(fds[1], fds[0]);
    close(fds[0]);
    close(fds[1]);
}

TEST_CASE("pipe()", "[sock_utils]")
{
    int fds[2];
    CHECK(esp_pipe(fds) == 0);
    printf("pipe created fds: %d, %d\n", fds[0], fds[1]);
    // check only one direction
    test_pipe(fds[0], fds[1]);
    close(fds[0]);
    close(fds[1]);
}

TEST_CASE("gai_strerror()", "[sock_utils]")
{
    const char *str_error = esp_gai_strerror(EAI_BADFLAGS);
    CHECK(str_error != NULL);
}


extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    Catch::Session session;

    int failures = session.run();
    if (failures > 0) {
        printf("TEST FAILED! number of failures=%d\n", failures);
        exit(1);
    } else {
        printf("Test passed!\n");
        exit(0);
    }
}
