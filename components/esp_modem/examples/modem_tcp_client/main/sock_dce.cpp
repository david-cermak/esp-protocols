// Copyright 2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <charconv>
#include <sys/socket.h>
#include "esp_vfs.h"
#include "esp_vfs_eventfd.h"

#include "sock_dce.hpp"

namespace sock_dce {

static const char *TAG = "sock_dce";


bool DCE::perform()
{
    struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 500000,
    };
    fd_set fdset;
    FD_ZERO(&fdset);
    if (listen_sock == -1) {
        ESP_LOGE(TAG, "Listening socket not ready");
        close_sock();
        return false;
    }
    if (sock == -1) {
        // need to accept the connection first
        FD_SET(listen_sock, &fdset);
        int s = select(listen_sock + 1, &fdset, nullptr, nullptr, &tv);
        if (s > 0 && FD_ISSET(listen_sock, &fdset)) {
            struct sockaddr_in source_addr = {};
            socklen_t addr_len = sizeof(source_addr);
            sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (sock < 0) {
                ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                return false;
            }
            ESP_LOGD(TAG, "Socket accepted!!!");
            FD_ZERO(&fdset);
            return true;
        } else if (s == 0) {
            return true;
        }
        return false;
    }
    FD_SET(sock, &fdset);
    FD_SET(data_ready_fd, &fdset);
    int s = select(std::max(sock, data_ready_fd) + 1, &fdset, nullptr, nullptr, &tv);
    if (s == 0) {
        ESP_LOGD(TAG, "perform select timeout...");
        return true;
    } else if (s < 0) {
        ESP_LOGE(TAG,  "select error %d", errno);
        close_sock();
        return false;
    }
    if (FD_ISSET(sock, &fdset)) {
        ESP_LOGD(TAG,  "socket read: data available");
        if (!signal.wait(IDLE, 1000)) {
            ESP_LOGE(TAG,  "Failed to get idle");
            close_sock();
            return false;
        }
        if (state != status::IDLE) {
            ESP_LOGE(TAG,  "Unexpected state %d", state);
            close_sock();
            return false;
        }
        state = status::SENDING;
        int len = ::recv(sock, &buffer[0], size, 0);
        if (len < 0) {
            ESP_LOGE(TAG,  "read error %d", errno);
            close_sock();
            return false;
        } else if (len == 0) {
            ESP_LOGE(TAG,  "EOF %d", errno);
            close_sock();
            return false;
        }
        ESP_LOG_BUFFER_HEXDUMP(TAG, &buffer[0], len, ESP_LOG_VERBOSE);
        data_to_send = len;
        send_cmd("AT+CIPSEND=0," + std::to_string(len) + "\r");
    }
    if (FD_ISSET(data_ready_fd, &fdset)) {
        uint64_t data;
        read(data_ready_fd, &data, sizeof(data));
        ESP_LOGD(TAG, "select read: modem data available %x", data);
        if (!signal.wait(IDLE, 1000)) {
            ESP_LOGE(TAG, "Failed to get idle");
            close_sock();
            return false;
        }
        if (state != status::IDLE) {
            ESP_LOGE(TAG, "Unexpected state %d", state);
            close_sock();
            return false;
        }
        state = status::RECEIVING;
        send_cmd("AT+CIPRXGET=2,0," + std::to_string(size) + "\r");
    }
    return true;
}

void DCE::forwarding(uint8_t *data, size_t len)
{
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, len, ESP_LOG_DEBUG);
    if (state == status::SENDING) {
        if (memchr(data, '>', len) == NULL) {
            ESP_LOGE(TAG, "Missed >");
            state = status::SENDING_FAILED;
            signal.set(IDLE);
            return;
        }
        auto written = dte->write(&buffer[0], data_to_send);
        if (written != data_to_send) {
            ESP_LOGE(TAG, "written %d (%d)...", written, len);
            state = status::SENDING_FAILED;
            signal.set(IDLE);
            return;
        }
        data_to_send = 0;
        uint8_t ctrl_z = '\x1A';
        dte->write(&ctrl_z, 1);
        state = status::SENDING_1;
        return;
    } else if (state == status::RECEIVING) {
        const size_t MIN_MESSAGE = 6;
        const std::string_view head = "+CIPRXGET: 2,0,";
        auto head_pos = (char *)std::search(data, data+len, head.begin(), head.end());
        if (head_pos == nullptr) {
            state = status::RECEIVING_FAILED;
            signal.set(IDLE);
            return;
        }
        if (head_pos - (char*)data > MIN_MESSAGE) {
            // check for async replies before the Recv header
            std::string_view response((char*)data, head_pos - (char*)data);
            check_async_replies(response);
        }

        auto next_comma = (char *)memchr(head_pos + head.size(), ',', MIN_MESSAGE);
        if (next_comma == nullptr)  {
            state = status::RECEIVING_FAILED;
            signal.set(IDLE);
            return;
        }
        size_t actual_len;
        if (std::from_chars(head_pos + head.size(), next_comma, actual_len).ec == std::errc::invalid_argument) {
            ESP_LOGE(TAG, "cannot convert");
            state = status::RECEIVING_FAILED;
            signal.set(IDLE);
            return;
        }

        ESP_LOGD(TAG, "Received: actual len=%d", actual_len);

        auto next_nl = (char*)memchr(next_comma, '\n', MIN_MESSAGE);
        if (next_nl == nullptr) {
            ESP_LOGE(TAG, "not found");
            state = status::RECEIVING_FAILED;
            signal.set(IDLE);
            return;
        }
        if (actual_len > size) {
            ESP_LOGE(TAG, "TOO BIG");
            state = status::RECEIVING_FAILED;
            signal.set(IDLE);
            return;
        }
        ::send(sock, next_nl+1, actual_len, 0);

        // "OK" after the data
        auto last_pos = (char*)memchr(next_nl+1+actual_len, 'O', MIN_MESSAGE);
        if (last_pos == nullptr || last_pos[1] != 'K') {
            state = status::RECEIVING_FAILED;
            signal.set(IDLE);
        }
        state = status::IDLE;
        signal.set(IDLE);
        if ((char*)data + len - last_pos > MIN_MESSAGE) {
            // check for async replies after the Recv header
            std::string_view response((char *)last_pos + 2 /* OK */, (char*)data + len - last_pos);
            check_async_replies(response);
        }
        return;
    }
    std::string_view response((char *)data, len);
    check_async_replies(response);
    // Notification about Data Ready could come any time
    if (state == status::SENDING_1) {
        if (response.find("+CIPSEND:") != std::string::npos) {
            state = status::IDLE;
            signal.set(IDLE);
            return;
        } else if (response.find("ERROR") != std::string::npos) {
            ESP_LOGE(TAG, "Failed to sent");
            state = status::SENDING_FAILED;
            signal.set(IDLE);
            return;
        }
    }
    if (state == status::CONNECTING) {
        if (response.find("+CIPOPEN: 0,0") != std::string::npos) {
            ESP_LOGI(TAG, "Connected!");
            state = status::IDLE;
            signal.set(IDLE);
            return;
        } else if (response.find("ERROR") != std::string::npos) {
            ESP_LOGE(TAG, "Failed to open");
            state = status::CONNECTION_FAILED;
            signal.set(IDLE);
            return;
        }
    }

}

void DCE::close_sock()
{
    if (sock > 0) {
        close(sock);
        sock = -1;
    }
}

void DCE::init(int port)
{
    esp_vfs_eventfd_config_t config = ESP_VFS_EVENTD_CONFIG_DEFAULT();
    esp_vfs_eventfd_register(&config);

    data_ready_fd = eventfd(0, EFD_SUPPORT_ISR);
    assert(data_ready_fd > 0);

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGI(TAG, "Socket created");
    struct sockaddr_in addr = {  };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
//    inet_aton("127.0.0.1", &addr.sin_addr);

    int err = bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        return;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", 1883);
    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        return;
    }

}

void DCE::check_async_replies(std::string_view &response) const
{
    ESP_LOGD(TAG, "response %.*s", static_cast<int>(response.size()), response.data());
    if (response.find("+CIPRXGET: 1") != std::string::npos) {
        uint64_t data_ready = 1;
        write(data_ready_fd, &data_ready, sizeof(data_ready));
        ESP_LOGD(TAG, "Got data on modem!");
    }

}

bool DCE::start(std::string host, int port)
{
    dte->on_read(nullptr);
    tcp_close();
    if (set_rx_mode(1) != esp_modem::command_result::OK) {
        ESP_LOGE(TAG, "Unable to set Rx mode");
        return false;
    }
    dte->on_read([this](uint8_t *data, size_t len) {
        this->forwarding(data, len);
        return esp_modem::command_result::TIMEOUT;
    });
    send_cmd(R"(AT+CIPOPEN=0,"TCP",")" + host + "\"," + std::to_string(port) + "\r");
    state = status::CONNECTING;
    return true;
}

bool DCE::init_network()
{
    const int retries = 5;
    int i = 0;
    while (sync() != esp_modem::command_result::OK) {
        if (i++ > retries) {
            ESP_LOGE(TAG, "Failed to sync up");
            return false;
        }
        esp_modem::Task::Delay(1000);
    }
    ESP_LOGD(TAG, "Modem in sync");
    i = 0;
    while (setup_data_mode() != true) {
        if (i++ > retries) {
            ESP_LOGE(TAG, "Failed to setup pdp/data");
            return false;
        }
        esp_modem::Task::Delay(1000);
    }
    ESP_LOGD(TAG, "PDP configured");
    i = 0;
    while (net_open() != esp_modem::command_result::OK) {
        if (i++ > retries) {
            ESP_LOGE(TAG, "Failed to open network");
            return false;
        }
        esp_modem::Task::Delay(1000);
    }
    ESP_LOGD(TAG, "Network opened");
    i = 0;
    std::string ip_addr;
    while (get_ip(ip_addr) != esp_modem::command_result::OK) {
        if (i++ > retries) {
            ESP_LOGE(TAG, "Failed obtain an IP address");
            return false;
        }
        esp_modem::Task::Delay(5000);
    }
    ESP_LOGI(TAG, "Got IP %s", ip_addr.c_str());
    return true;
}


class Factory: public ::esp_modem::dce_factory::Factory {
public:
    static std::unique_ptr<DCE> create(const esp_modem::dce_config *config, std::shared_ptr<esp_modem::DTE> dte)
    {
        return esp_modem::dce_factory::Factory::build_module_T<DCE, std::unique_ptr<DCE>>(config, std::move(dte));
    }
};

std::unique_ptr<DCE> create(const esp_modem::dce_config *config, std::shared_ptr<esp_modem::DTE> dte)
{
    return Factory::create(config, std::move(dte));
}

// Helper macros to handle multiple arguments of declared API
#define ARGS0
#define ARGS1 , p1
#define ARGS2 , p1 , p2
#define ARGS3 , p1 , p2 , p3

#define EXPAND_ARGS(x)  ARGS ## x
#define ARGS(x)  EXPAND_ARGS(x)

//
// Repeat all declarations and forward to the AT commands defined in ::sock_commands namespace
//
#define ESP_MODEM_DECLARE_DCE_COMMAND(name, return_type, arg_nr, ...) \
     esp_modem::return_type DCE::name(__VA_ARGS__) { return sock_commands::name(dte.get() ARGS(arg_nr) ); }

DECLARE_SOCK_COMMANDS(return_type name(...) )

#undef ESP_MODEM_DECLARE_DCE_COMMAND

} // namespace sock_dce