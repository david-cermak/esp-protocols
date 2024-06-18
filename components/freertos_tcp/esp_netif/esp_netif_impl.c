/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
//
// Created by david on 6/17/24.
//
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "interface.h"
#include "NetworkInterface.h"
#include "esp_log.h"
#include "esp_event.h"

#define MAX_ENDPOINTS_PER_NETIF     4

static const char *TAG = "esp_netif_AFpT";
static bool s_netif_initialized = false;


struct esp_netif_stack {
    esp_netif_netstack_config_t config;
    NetworkInterface_t aft_netif;
    NetworkEndPoint_t xEndPoints[MAX_ENDPOINTS_PER_NETIF];
};


struct esp_netif_obj {
    uint8_t mac[6];
    // io driver related
    void *driver_handle;
    esp_err_t (*driver_transmit)(void *h, void *buffer, size_t len);
    void (*driver_free_rx_buffer)(void *h, void *buffer);

    // stack related
    struct esp_netif_stack *net_stack;

    // misc flags, types, keys, priority
    esp_netif_flags_t flags;
    char *hostname;
    char *if_key;
    char *if_desc;
    int route_prio;
};

static uint8_t ucMACAddress[ 6 ] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };

/* Define the network addressing.  These parameters will be used if either
   ipconfigUDE_DHCP is 0 or if ipconfigUSE_DHCP is 1 but DHCP auto configuration
   failed. */
static const uint8_t ucIPAddress[ 4 ] = { 10, 10, 10, 200 };
static const uint8_t ucNetMask[ 4 ] = { 255, 0, 0, 0 };
static const uint8_t ucGatewayAddress[ 4 ] = { 10, 10, 10, 1 };
static const uint8_t ucDNSServerAddress[ 4 ] = { 208, 67, 222, 222 };

void *esp_netif_new_netstack_if(esp_netif_t *esp_netif, const esp_netif_netstack_config_t *cfg)
{
    static int netif_count = 0;
    struct esp_netif_stack *netif = calloc(1, sizeof(struct esp_netif_stack));
    if (!netif) {
        return NULL;
    }
    netif->config.init_fn = cfg->init_fn;
    if (!cfg->init_fn(netif_count++, &netif->aft_netif)) {
        free(netif);
        return NULL;
    }
//    ESP_LOGW(TAG, "%d %p", netif_count, netif->config.init_fn);
//    FreeRTOS_IPInit_Multi();
    FreeRTOS_FillEndPoint( &( netif->aft_netif ), &( netif->xEndPoints[0]), ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress );
    netif->xEndPoints[ 0 ].bits.bWantDHCP = pdTRUE;
    netif->aft_netif.bits.bInterfaceUp = 0;
    //    netif->xEndPoints[ 0 ].bits.bEndPointUp = pdFALSE;
    FreeRTOS_IPInit_Multi();

    netif->aft_netif.pvArgument = esp_netif;
    netif->config.input_fn = cfg->input_fn;
    return netif;
}


esp_err_t esp_netif_receive(esp_netif_t *esp_netif, void *buffer, size_t len, void *eb)
{
    struct esp_netif_stack *netif = esp_netif->net_stack;
//    ESP_LOGW(TAG, "esp_netif_receive() %d %p", len, netif->config.input_fn);
    return netif->config.input_fn(&netif->aft_netif, buffer, len, eb);
}

void vNetworkNotifyIFUp(NetworkInterface_t *pxInterface);

//esp_err_t esp_netif_update_netstack_if(void* netif, esp_netif_status_update_t status)
//{
//    if (status == ESP_NETIF_STATUS_CONNECTED) {
//        vNetworkNotifyIFUp();
//    }
//    return ESP_OK;
//}

void esp_netif_set_ip4_addr(esp_ip4_addr_t *addr, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    memset(addr, 0, sizeof(esp_ip4_addr_t));
    addr->addr = esp_netif_htonl(esp_netif_ip4_makeu32(a, b, c, d));
}

char *esp_ip4addr_ntoa(const esp_ip4_addr_t *addr, char *buf, int buflen)
{
    return NULL;
}

esp_netif_iodriver_handle esp_netif_get_io_driver(esp_netif_t *esp_netif)
{
    return esp_netif->driver_handle;
}

esp_netif_t *esp_netif_get_handle_from_netif_impl(void *dev)
{
    return NULL;
}

esp_err_t esp_netif_init(void)
{
    ESP_LOGI(TAG, "loopback initialization");
    if (s_netif_initialized) {
        ESP_LOGE(TAG, "esp-netif has already been initialized");
        return ESP_ERR_INVALID_SIZE;
    }

    s_netif_initialized = true;
    ESP_LOGD(TAG, "esp-netif has been successfully initialized");
    return ESP_OK;
}

esp_err_t esp_netif_deinit(void)
{
    ESP_LOGI(TAG, "loopback initialization");
    if (!s_netif_initialized) {
        ESP_LOGE(TAG, "esp-netif has not been initialized yet");
        return ESP_ERR_INVALID_SIZE;
    }
    s_netif_initialized = false;
    ESP_LOGD(TAG, "esp-netif has been successfully deinitialized");
    return ESP_OK;
}

static esp_err_t esp_netif_init_configuration(esp_netif_t *esp_netif, const esp_netif_config_t *cfg)
{
    // Basic esp_netif and lwip is a mandatory configuration and cannot be updated after esp_netif_new()
    if (cfg == NULL || cfg->base == NULL || cfg->stack == NULL) {
        return ESP_ERR_ESP_NETIF_INVALID_PARAMS;
    }

    // Setup main config parameters
    esp_netif->flags = cfg->base->flags;

    if (cfg->base->if_key) {
        esp_netif->if_key = strdup(cfg->base->if_key);
    }
    if (cfg->base->if_desc) {
        esp_netif->if_desc = strdup(cfg->base->if_desc);
    }
    if (cfg->base->route_prio) {
        esp_netif->route_prio = cfg->base->route_prio;
    }

    // Network stack configs
    esp_netif->net_stack = esp_netif_new_netstack_if(esp_netif, cfg->stack);

    // Install IO functions only if provided -- connects driver and netif
    // this configuration could be updated after esp_netif_new(), typically in post_attach callback
    if (cfg->driver) {
        const esp_netif_driver_ifconfig_t *esp_netif_driver_config = cfg->driver;
        if (esp_netif_driver_config->handle) {
            esp_netif->driver_handle = esp_netif_driver_config->handle;
        }
        if (esp_netif_driver_config->transmit) {
            esp_netif->driver_transmit = esp_netif_driver_config->transmit;
        }
        if (esp_netif_driver_config->driver_free_rx_buffer) {
            esp_netif->driver_free_rx_buffer = esp_netif_driver_config->driver_free_rx_buffer;
        }
    }
    return ESP_OK;
}

esp_netif_t *esp_netif_new(const esp_netif_config_t *esp_netif_config)
{
    // mandatory configuration must be provided when creating esp_netif object
    if (esp_netif_config == NULL) {
        return NULL;
    }

    // Create parent esp-netif object
    esp_netif_t *esp_netif = calloc(1, sizeof(struct esp_netif_obj));
    if (!esp_netif) {
        return NULL;
    }

    // Create ip info
    esp_netif_ip_info_t *ip_info = calloc(1, sizeof(esp_netif_ip_info_t));
    if (!ip_info) {
        free(esp_netif);
        return NULL;
    }

    // creating another ip info (to store old ip)
//    esp_netif_add_to_list_unsafe(esp_netif);

    // Configure the created object with provided configuration
    esp_err_t ret =  esp_netif_init_configuration(esp_netif, esp_netif_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initial configuration of esp_netif failed with %d", ret);
        esp_netif_destroy(esp_netif);
        return NULL;
    }

    return esp_netif;
}

void esp_netif_destroy(esp_netif_t *esp_netif)
{
    if (esp_netif) {
//        esp_netif_remove_from_list_unsafe(esp_netif);
        free(esp_netif->if_key);
        free(esp_netif->if_desc);
        free(esp_netif);
    }
}

esp_err_t esp_netif_attach(esp_netif_t *esp_netif, esp_netif_iodriver_handle driver_handle)
{
    esp_netif_driver_base_t *base_driver = driver_handle;

    esp_netif->driver_handle = driver_handle;
    if (base_driver->post_attach) {
        esp_err_t ret = base_driver->post_attach(esp_netif, driver_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Post-attach callback of driver(%p) failed with %d", driver_handle, ret);
            return ESP_ERR_ESP_NETIF_DRIVER_ATTACH_FAILED;
        }
    }
    return ESP_OK;
}

esp_err_t esp_netif_set_driver_config(esp_netif_t *esp_netif,
                                      const esp_netif_driver_ifconfig_t *driver_config)
{
    if (esp_netif == NULL || driver_config == NULL) {
        return ESP_ERR_ESP_NETIF_INVALID_PARAMS;
    }
    esp_netif->driver_handle = driver_config->handle;
    esp_netif->driver_transmit = driver_config->transmit;
    esp_netif->driver_free_rx_buffer = driver_config->driver_free_rx_buffer;
    return ESP_OK;
}

esp_err_t esp_netif_set_mac(esp_netif_t *esp_netif, uint8_t ucMACAddress[])
{
    ESP_LOGI(TAG, "esp_netif_set_mac()");
    memcpy(esp_netif->mac, ucMACAddress, 6);
//    uint8_t ucMACAddress[ ipMAC_ADDRESS_LENGTH_BYTES ];

//    FreeRTOS_UpdateMACAddress( ucMACAddress );

    return ESP_OK;
}

esp_err_t esp_netif_start(esp_netif_t *esp_netif)
{
    ESP_LOGI(TAG, "Netif started");
    memcpy(&esp_netif->net_stack->xEndPoints->xMACAddress, esp_netif->mac, 6);
//    FreeRTOS_UpdateMACAddress( esp_netif->mac );

//    vNetworkNotifyIFUp(&esp_netif->net_stack->aft_netif);
    return ESP_OK;
}


esp_err_t esp_netif_stop(esp_netif_t *esp_netif)
{
    ESP_LOGI(TAG, "Netif stopped");
    return ESP_OK;
}

//
// IO translate functions
//
void esp_netif_free_rx_buffer(void *h, void *buffer)
{
    esp_netif_t *esp_netif = h;
    esp_netif->driver_free_rx_buffer(esp_netif->driver_handle, buffer);
}

esp_err_t esp_netif_transmit(esp_netif_t *esp_netif, void *data, size_t len)
{
    ESP_LOGW(TAG, "Transmitting data: ptr:%p, size:%lu", data, (long unsigned int) len);
    return (esp_netif->driver_transmit)(esp_netif->driver_handle, data, len);
}

esp_err_t esp_netif_dhcpc_stop(esp_netif_t *esp_netif)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_dhcpc_start(esp_netif_t *esp_netif)
{
//    esp_netif->net_stack->xEndPoints[ 0 ].bits.bWantDHCP = pdTRUE;
    return ESP_OK;
}

esp_err_t esp_netif_dhcps_get_status(esp_netif_t *esp_netif, esp_netif_dhcp_status_t *status)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_dhcpc_get_status(esp_netif_t *esp_netif, esp_netif_dhcp_status_t *status)
{
    *status = ESP_NETIF_DHCP_INIT;
    return ESP_OK;
}

esp_err_t esp_netif_dhcps_start(esp_netif_t *esp_netif)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_dhcps_stop(esp_netif_t *esp_netif)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_set_hostname(esp_netif_t *esp_netif, const char *hostname)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_get_hostname(esp_netif_t *esp_netif, const char **hostname)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_up(esp_netif_t *esp_netif)
{
    ESP_LOGI(TAG, "Netif going up");
    struct esp_netif_stack *netif = esp_netif->net_stack;
    netif->aft_netif.bits.bInterfaceUp = 1;
    vNetworkNotifyIFUp(&esp_netif->net_stack->aft_netif);

//    vNetworkNotifyIFUp();
    return ESP_OK;
}

esp_err_t esp_netif_down(esp_netif_t *esp_netif)
{
    ESP_LOGI(TAG, "Netif going down");
    return ESP_OK;
}

bool esp_netif_is_netif_up(esp_netif_t *esp_netif)
{
    return false;
}

esp_err_t esp_netif_get_old_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info)
{
    ESP_LOGD(TAG, "%s esp_netif:%p", __func__, esp_netif);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info)
{
    ESP_LOGD(TAG, "%s esp_netif:%p", __func__, esp_netif);

    return ESP_ERR_NOT_SUPPORTED;
}

bool esp_netif_is_valid_static_ip(esp_netif_ip_info_t *ip_info)
{
    return true;
}

esp_err_t esp_netif_set_old_ip_info(esp_netif_t *esp_netif, const esp_netif_ip_info_t *ip_info)
{
    ESP_LOGD(TAG, "%s esp_netif:%p", __func__, esp_netif);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_set_dns_info(esp_netif_t *esp_netif, esp_netif_dns_type_t type, esp_netif_dns_info_t *dns)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_get_dns_info(esp_netif_t *esp_netif, esp_netif_dns_type_t type, esp_netif_dns_info_t *dns)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_create_ip6_linklocal(esp_netif_t *esp_netif)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_get_ip6_linklocal(esp_netif_t *esp_netif, esp_ip6_addr_t *if_ip6)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_get_ip6_global(esp_netif_t *esp_netif, esp_ip6_addr_t *if_ip6)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_netif_flags_t esp_netif_get_flags(esp_netif_t *esp_netif)
{
    return esp_netif->flags;
}

const char *esp_netif_get_ifkey(esp_netif_t *esp_netif)
{
    return esp_netif->if_key;
}

const char *esp_netif_get_desc(esp_netif_t *esp_netif)
{
    return esp_netif->if_desc;
}

int32_t esp_netif_get_event_id(esp_netif_t *esp_netif, esp_netif_ip_event_type_t event_type)
{
    return 0;
}

esp_err_t esp_netif_dhcps_option(esp_netif_t *esp_netif, esp_netif_dhcp_option_mode_t opt_op, esp_netif_dhcp_option_id_t opt_id, void *opt_val,
                                 uint32_t opt_len)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_dhcpc_option(esp_netif_t *esp_netif, esp_netif_dhcp_option_mode_t opt_op, esp_netif_dhcp_option_id_t opt_id, void *opt_val,
                                 uint32_t opt_len)
{
    return ESP_ERR_NOT_SUPPORTED;
}

int esp_netif_get_netif_impl_index(esp_netif_t *esp_netif)
{
    return 0;
}

esp_err_t esp_netif_join_ip6_multicast_group(esp_netif_t *esp_netif, const esp_ip6_addr_t *addr)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_leave_ip6_multicast_group(esp_netif_t *esp_netif, const esp_ip6_addr_t *addr)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_add_ip6_address(esp_netif_t *esp_netif, const esp_ip6_addr_t addr, bool preferred)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_netif_remove_ip6_address(esp_netif_t *esp_netif, const esp_ip6_addr_t *addr)
{
    return ESP_ERR_NOT_SUPPORTED;
}

int esp_netif_get_all_ip6(esp_netif_t *esp_netif, esp_ip6_addr_t if_ip6[])
{
    return 0;
}

esp_ip6_addr_type_t esp_netif_ip6_get_addr_type(esp_ip6_addr_t *ip6_addr)
{
    return ESP_IP6_ADDR_IS_UNKNOWN;
}

esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn fn, void *ctx)
{
    return fn(ctx);
}

/* Called by FreeRTOS+TCP when the network connects or disconnects.  Disconnect
 * events are only received if implemented in the MAC driver. */
void vApplicationIPNetworkEventHook_Multi( eIPCallbackEvent_t eNetworkEvent,
        struct xNetworkEndPoint *pxEndPoint )
{
    uint32_t ulIPAddress = 0, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
    char cBuffer[ 16 ];
    static BaseType_t xTasksAlreadyCreated = pdFALSE;

    /* If the network has just come up...*/
    if ( eNetworkEvent == eNetworkUp ) {
        /* Create the tasks that use the IP stack if they have not already been
         * created. */
        if ( xTasksAlreadyCreated == pdFALSE ) {
            /* See the comments above the definitions of these pre-processor
             * macros at the top of this file for a description of the individual
             * demo tasks. */

#if ( mainCREATE_TCP_ECHO_TASKS_SINGLE == 1 )
            {
                vStartTCPEchoClientTasks_SingleTasks( mainECHO_CLIENT_TASK_STACK_SIZE, mainECHO_CLIENT_TASK_PRIORITY );
            }
#endif /* mainCREATE_TCP_ECHO_TASKS_SINGLE */

            xTasksAlreadyCreated = pdTRUE;
        }

        /* Print out the network configuration, which may have come from a DHCP
         * server. */
#if defined( ipconfigIPv4_BACKWARD_COMPATIBLE ) && ( ipconfigIPv4_BACKWARD_COMPATIBLE == 0 )
        FreeRTOS_GetEndPointConfiguration( &ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress, pxNetworkEndPoints );
#else
        FreeRTOS_GetAddressConfiguration( &ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress );
#endif /* defined( ipconfigIPv4_BACKWARD_COMPATIBLE ) && ( ipconfigIPv4_BACKWARD_COMPATIBLE == 0 ) */
        FreeRTOS_inet_ntoa( ulIPAddress, cBuffer );
        printf( "\r\n\r\nIP Address: %s\r\n", cBuffer );

        FreeRTOS_inet_ntoa( ulNetMask, cBuffer );
        printf( "Subnet Mask: %s\r\n", cBuffer );

        FreeRTOS_inet_ntoa( ulGatewayAddress, cBuffer );
        printf( "Gateway Address: %s\r\n", cBuffer );

        FreeRTOS_inet_ntoa( ulDNSServerAddress, cBuffer );
        printf( "DNS Server Address: %s\r\n\r\n\r\n", cBuffer );
        ip_event_got_ip_t evt = {
            .esp_netif = pxEndPoint->pxNetworkInterface->pvArgument,
            .ip_changed = false,
        };

        evt.ip_info.ip.addr = ulIPAddress;
        evt.ip_info.gw.addr = ulGatewayAddress;
        evt.ip_info.netmask.addr = ulNetMask;
        esp_err_t  ret = esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, &evt, sizeof(evt), 0);
        if (ESP_OK != ret) {
            ESP_LOGE(TAG, "dhcpc cb: failed to post got ip event (%x)", ret);
        }
    } else {
        printf( "Application idle hook network down\n" );
    }
}
