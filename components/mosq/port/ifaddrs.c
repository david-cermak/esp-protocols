/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "ifaddrs.h"

// Dummy implementation of getifaddrs()
// TODO: Implement this if we need to use bind_interface option of listener's config
int getifaddrs(struct ifaddrs **ifap)
{
    return -1;
}

void freeifaddrs(struct ifaddrs *ifa)
{

}
