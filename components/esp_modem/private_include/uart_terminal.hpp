/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <esp_modem_config.h>
#include "cxx_include/esp_modem_dte.hpp"

struct esp_modem_dte_config;

namespace esp_modem {

std::unique_ptr<Terminal> create_uart_terminal(const esp_modem_dte_config *config);

esp_err_t set_uart_term_flow_control(DTE *dte, esp_modem_flow_ctrl_t flow_control);

}  // namespace esp_modem
