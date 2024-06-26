/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <ctype.h>

#undef  isspace
#define isspace(__c) (__ctype_lookup((int)__c)&_S)

#include_next "config.h"

#define VERSION "0.1.0"

//#define sysconf(x) (64)

static inline long sysconf(int arg)
{
    return 64;
}
