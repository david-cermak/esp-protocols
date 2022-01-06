// Copyright 2021 Espressif Systems (Shanghai) CO LTD
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
// limitations under the License

/**
 * @file esp_pbuf reference interface file
 */

#ifndef __LWIP_ESP_PBUF_REF_H__
#define __LWIP_ESP_PBUF_REF_H__

#include <stddef.h>
#include "lwip/pbuf.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Allocate custom pbuf containing pointer to a private l2-free function
 *
 * @note pbuf_free() will deallocate this custom pbuf and call the driver assigned free function
 */
struct pbuf* esp_pbuf_allocate(esp_netif_t *esp_netif, void *buffer, size_t len, void *l2_buff);

#ifdef __cplusplus
}
#endif

#endif //__LWIP_ESP_PBUF_REF_H__
