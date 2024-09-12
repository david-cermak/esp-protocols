/*
 * SPDX-FileCopyrightText: 2024 Roger Light <roger@atchoo.org>
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * SPDX-FileContributor: 2024 Espressif Systems (Shanghai) CO LTD
 */
#include "mosquitto_internal.h"
#include "mosquitto_broker.h"
#include "memory_mosq.h"
#include "mqtt_protocol.h"
#include "send_mosq.h"
#include "util_mosq.h"
#include "utlist.h"
#include "lib_load.h"


int mosquitto_callback_register(
    mosquitto_plugin_id_t *identifier,
    int event,
    MOSQ_FUNC_generic_callback cb_func,
    const void *event_data,
    void *userdata)
{
    struct mosquitto__callback **cb_base = NULL, *cb_new;
    struct mosquitto__security_options *security_options;

    if (cb_func == NULL) {
        return MOSQ_ERR_INVAL;
    }

    if (identifier->listener == NULL) {
        security_options = &db.config->security_options;
    } else {
        security_options = &identifier->listener->security_options;
    }

    switch (event) {
    case MOSQ_EVT_RELOAD:
        cb_base = &security_options->plugin_callbacks.reload;
        break;
    case MOSQ_EVT_ACL_CHECK:
        cb_base = &security_options->plugin_callbacks.acl_check;
        break;
    case MOSQ_EVT_BASIC_AUTH:
        cb_base = &security_options->plugin_callbacks.basic_auth;
        break;
    case MOSQ_EVT_PSK_KEY:
        cb_base = &security_options->plugin_callbacks.psk_key;
        break;
    case MOSQ_EVT_EXT_AUTH_START:
        cb_base = &security_options->plugin_callbacks.ext_auth_start;
        break;
    case MOSQ_EVT_EXT_AUTH_CONTINUE:
        cb_base = &security_options->plugin_callbacks.ext_auth_continue;
        break;
    case MOSQ_EVT_CONTROL:
        return control__register_callback(security_options, cb_func, event_data, userdata);
        break;
    case MOSQ_EVT_MESSAGE:
        cb_base = &security_options->plugin_callbacks.message;
        break;
    case MOSQ_EVT_TICK:
        cb_base = &security_options->plugin_callbacks.tick;
        break;
    case MOSQ_EVT_DISCONNECT:
        cb_base = &security_options->plugin_callbacks.disconnect;
        break;
    default:
        return MOSQ_ERR_NOT_SUPPORTED;
        break;
    }

    cb_new = mosquitto__calloc(1, sizeof(struct mosquitto__callback));
    if (cb_new == NULL) {
        return MOSQ_ERR_NOMEM;
    }
    DL_APPEND(*cb_base, cb_new);
    cb_new->cb = cb_func;
    cb_new->userdata = userdata;

    return MOSQ_ERR_SUCCESS;
}

int mosquitto_callback_unregister(
    mosquitto_plugin_id_t *identifier,
    int event,
    MOSQ_FUNC_generic_callback cb_func,
    const void *event_data)
{
    return MOSQ_ERR_INVAL;
}

void plugin__handle_tick(void)
{
}

void plugin__handle_disconnect(struct mosquitto *context, int reason)
{
}

int plugin__handle_message(struct mosquitto *context, struct mosquitto_msg_store *stored)
{
    printf("plugin__handle_message\n");
    return MOSQ_ERR_SUCCESS;
}
