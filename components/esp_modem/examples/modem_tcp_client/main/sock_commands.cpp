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
#include <cstring>
#include "sock_commands.hpp"

namespace sock_commands {

static const char *TAG = "sock_commands";

using namespace esp_modem;

command_result net_open(CommandableIf *t)
{
    ESP_LOGV(TAG, "%s", __func__ );
    std::string_view out;
    auto ret = dce_commands::generic_get_string(t, "AT+NETOPEN?\r", out, 1000);
    if (ret != command_result::OK) {
        return ret;
    }
    ESP_LOGV(TAG, "%s", out.data() );
    if (out.find("+NETOPEN: 1") != std::string::npos) {
        ESP_LOGD(TAG, "Already there");
        return command_result::OK;
    } else if (out.find("+NETOPEN: 0") != std::string::npos) {
        ESP_LOGW(TAG, "Need to setup");
        return dce_commands::generic_command(t, "AT+NETOPEN\r", "+NETOPEN: 1", "+NETOPEN: 0", 10000);
    }
    return command_result::FAIL;
}

command_result net_close(CommandableIf *t)
{
    ESP_LOGW(TAG, "%s", __func__ );
    return dce_commands::generic_command(t, "AT+NETCLOSE\r", "+NETCLOSE:", "ERROR", 30000);
}

command_result tcp_open(CommandableIf *t, const std::string& host, int port, int timeout)
{
    ESP_LOGW(TAG, "%s", __func__ );
    auto ret = dce_commands::generic_command(t, "AT+CIPRXGET=1\r", "OK", "ERROR", 50000);
    if (ret != command_result::OK) {
        ESP_LOGE(TAG, "Setting Rx mode failed!");
        return ret;
    }
    ESP_LOGW(TAG, "%s", __func__ );
    std::string ip_open = R"(AT+CIPOPEN=0,"TCP",")" + host + "\"," + std::to_string(port) + "\r";
    ret = dce_commands::generic_command(t, ip_open, "+CIPOPEN: 0,0", "ERROR", timeout);
    if (ret != command_result::OK) {
        ESP_LOGE(TAG, "%s Failed", __func__ );
        return ret;
    }
    return command_result::OK;
}

command_result tcp_close(CommandableIf *t)
{
    ESP_LOGW(TAG, "%s", __func__ );
    return dce_commands::generic_command(t, "AT+CIPCLOSE=0\r", "+CIPCLOSE:", "ERROR", 10000);
}

command_result tcp_send(CommandableIf *t, uint8_t *data, size_t len)
{
    ESP_LOGW(TAG, "%s", __func__ );
    std::string send = "AT+CIPSEND=0," + std::to_string(len) + "\r";
    auto ret = t->command(send, [&](uint8_t *data, size_t len) {
        std::string_view response((char *)data, len);
        ESP_LOGI(TAG, "CIPSEND response %.*s", static_cast<int>(response.size()), response.data());
        if (response.find('>') != std::string::npos) {
            return command_result::OK;
        }
        return command_result::TIMEOUT;
    }, 50000, '>');
    if (ret != command_result::OK) {
        return ret;
    }
    ret = command_result::TIMEOUT;
    ESP_LOGW(TAG, "Before setting...");
    t->on_read([&ret](uint8_t *cmd_data, size_t cmd_len) {
        std::string_view response((char *)cmd_data, cmd_len);
        ESP_LOGW(TAG, "CIPSEND response %.*s", static_cast<int>(response.size()), response.data());

        if (response.find("+CIPSEND:") != std::string::npos) {
            ret = command_result::OK;
        } else if (response.find("ERROR") != std::string::npos) {
            ret = command_result::FAIL;
        }
        return ret;
    });
    ESP_LOGW(TAG, "Before writing...");
    auto written = t->write(data, len);
    if (written != len) {
        ESP_LOGE(TAG, "written %d (%d)...", written, len);
        return command_result::FAIL;
    }
    uint8_t ctrl_z = '\x1A';
    t->write(&ctrl_z, 1);
    int count = 0;
    while (ret == command_result::TIMEOUT && count++ < 1000 ) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    t->on_read(nullptr);
    return ret;
//    return t->command("", [&](uint8_t *cmd_data, size_t cmd_len) {
//        uint8_t ctrl_z = '\x1A';
//        t->write(&ctrl_z, 1);
//        std::string_view response((char *)cmd_data, cmd_len);
//        ESP_LOGI(TAG, "CIPSEND response %.*s", static_cast<int>(response.size()), response.data());
//        if (response.find("+CIPSEND:") != std::string::npos ||
//            response.find("OK") != std::string::npos ) {
//            return command_result::OK;
//        } else if (response.find("ERROR") != std::string::npos) {
//            return command_result::FAIL;
//        }
//        return command_result::TIMEOUT;
//    }, 50000);
    return command_result::OK;
//    size_t cmd_len = send.size();
//    send.resize(cmd_len + len + 1);
//    memcpy((char*)send.c_str() + cmd_len, data, len);
//    *((char*)send.c_str() + cmd_len + 1) = '\x1A';
//    return generic_command(t, send, "+CIPSEND:", "ERROR", 120000);
}

command_result tcp_recv(CommandableIf *t, uint8_t *data, size_t len, size_t &out_len)
{
    ESP_LOGW(TAG, "%s", __func__ );
//    auto ret = generic_command_common(t, "AT+CIPRXGET=1\n");
//    if (ret != command_result::OK) {
//        ESP_LOGE(TAG, "%s Failed", __func__ );
//        return ret;
//    }
    std::string_view out;
    auto ret = dce_commands::generic_get_string(t, "AT+CIPRXGET=4,0\r", out);
    if (ret != command_result::OK) {
        return ret;
    }
    constexpr std::string_view pattern = "+CIPRXGET: 4,0,";
    if (out.find(pattern) == std::string::npos) {

        return command_result::FAIL;
    }
    size_t data_len;
    if (std::from_chars(out.data() + pattern.size(), out.data() + out.size(), data_len).ec == std::errc::invalid_argument) {
        return command_result::FAIL;
    }
    ESP_LOGI(TAG, "size=%d", data_len);
    if (data_len == 0) {
        out_len = data_len;
        return command_result::OK;
    }
    return t->command("AT+CIPRXGET=2,0,100\r", [&](uint8_t *cmd_data, size_t cmd_len) {
        char pattern[] = "+CIPRXGET: 2,0,";
        ESP_LOG_BUFFER_HEXDUMP(TAG, cmd_data, cmd_len, ESP_LOG_ERROR);
        char* pos = strstr((char*)cmd_data, pattern);
        if (pos == nullptr) {
            return command_result::FAIL;
        }
        auto p1 = memchr(pos + sizeof(pattern) - 1, ',', 4);
        if (p1 == nullptr)  {
            return command_result::FAIL;
        }
        *(char*)p1 = '\0';
        size_t actual_len = atoi(pos + sizeof(pattern) - 1);
        ESP_LOGI(TAG, "actual len=%d", actual_len);

        pos = strchr((char*)p1+1, '\n');
        if (pos == nullptr) {
            ESP_LOGE(TAG, "not found");
            return command_result::FAIL;
        }
        if (actual_len > len) {
            ESP_LOGE(TAG, "TOO BIG");
            return command_result::FAIL;
        }
        out_len = actual_len;
        memcpy(data, pos+1, actual_len);
        pos = strstr((char*)pos+1+actual_len, "OK");
        if (pos == nullptr) {
            ESP_LOGE(TAG, "ok NOT FOUND");
            return command_result::FAIL;
        }
        return command_result::OK;
    }, 50000);
}

command_result get_ip(CommandableIf *t, std::string& ip)
{
    std::string_view resp;
    auto ret = dce_commands::generic_get_string(t, "AT+IPADDR\r", resp, 5000);
    if (ret != command_result::OK) {
        return ret;
    }
    ip = resp;
    return command_result::OK;
}

command_result set_rx_mode(CommandableIf *t, int mode)
{
    return dce_commands::generic_command(t, "AT+CIPRXGET=" + std::to_string(mode) + "\r", "OK", "ERROR", 5000);
}



}

