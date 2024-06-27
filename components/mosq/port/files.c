/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ctype.h>
#include "mosquitto.h"
#include "mosquitto_broker_internal.h"

// Dummy implementation of file access
// This needs to be implemented if we need to load/store config from files
FILE *mosquitto__fopen(const char *path, const char *mode, bool restrict_read)
{
    return NULL;
}

char *fgets_extending(char **buf, int *buflen, FILE *stream)
{
    return NULL;
}
