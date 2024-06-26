/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ctype.h>
#include "mosquitto.h"
#include "mosquitto_broker_internal.h"

FILE *mosquitto__fopen(const char *path, const char *mode, bool restrict_read)
{
    return NULL;
}

char *fgets_extending(char **buf, int *buflen, FILE *stream)
{
    return NULL;
}

char *misc__trimblanks(char *str)
{
    char *endptr;

    if (str == NULL) {
        return NULL;
    }

    while (isspace((int)str[0])) {
        str++;
    }
    endptr = &str[strlen(str) - 1];
    while (endptr > str && isspace((int)endptr[0])) {
        endptr[0] = '\0';
        endptr--;
    }
    return str;
}
