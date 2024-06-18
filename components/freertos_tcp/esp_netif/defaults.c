/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
//
// Created by david on 6/17/24.
//
#include "esp_netif.h"

struct netif;


esp_err_t wlanif_init_ap(struct netif *netif);
esp_err_t wlanif_init_sta(struct netif *netif);
esp_err_t wlanif_input(void *h, void *buffer, size_t len, void *l2_buff);
