/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "signal.h"

int sigprocmask (int, const sigset_t *, sigset_t *)
{
    return 0;
}
