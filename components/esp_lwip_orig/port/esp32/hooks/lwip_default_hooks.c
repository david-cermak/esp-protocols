// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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
// limitations under the License.

#include "lwip_default_hooks.h"

#define __weak __attribute__((weak))

#ifdef CONFIG_LWIP_HOOK_IP6_ROUTE_DEFAULT
struct netif *__weak
lwip_hook_ip6_route(const ip6_addr_t *src, const ip6_addr_t *dest)
{
    LWIP_UNUSED_ARG(src);
    LWIP_UNUSED_ARG(dest);

    return NULL;
}
#endif

#ifdef CONFIG_LWIP_HOOK_NETCONN_EXT_RESOLVE_DEFAULT
int __weak lwip_hook_netconn_external_resolve(const char *name, ip_addr_t *addr, u8_t addrtype, err_t *err)
{
    LWIP_UNUSED_ARG(name);
    LWIP_UNUSED_ARG(addr);
    LWIP_UNUSED_ARG(addrtype);
    LWIP_UNUSED_ARG(err);

    return 0;
}
#endif

#ifdef CONFIG_LWIP_HOOK_ND6_GET_GW_DEFAULT
const ip6_addr_t *__weak lwip_hook_nd6_get_gw(struct netif *netif, const ip6_addr_t *dest)
{
    LWIP_UNUSED_ARG(netif);
    LWIP_UNUSED_ARG(dest);

    return 0;
}
#endif

#ifdef LWIP_HOOK_IP4_ROUTE_SRC
#if ESP_IP4_ROUTE
#include "lwip/netif.h"

bool ip4_netif_exist(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    struct netif *netif = NULL;

    for (netif = netif_list; netif != NULL; netif = netif->next) {
        /* is the netif up, does it have a link and a valid address? */
        if (netif_is_up(netif) && netif_is_link_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
            /* source netif and dest netif match? */
            if (ip4_addr_netcmp(src, netif_ip4_addr(netif), netif_ip4_netmask(netif)) || ip4_addr_netcmp(dest, netif_ip4_addr(netif), netif_ip4_netmask(netif))) {
                /* return false when both netif don't match */
                return true;
            }
        }
    }

    return false;
}

/**
 * Source based IPv4 routing hook function.
 */
struct netif *
ip4_route_src_hook(const ip4_addr_t *src,const ip4_addr_t *dest)
{
    struct netif *netif = NULL;

    /* destination IP is broadcast IP? */
    if ((src != NULL) && !ip4_addr_isany(src)) {
        /* iterate through netifs */
        for (netif = netif_list; netif != NULL; netif = netif->next) {
            /* is the netif up, does it have a link and a valid address? */
            if (netif_is_up(netif) && netif_is_link_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
                /* source IP matches? */
                if (ip4_addr_cmp(src, netif_ip4_addr(netif))) {
                    /* return netif on which to forward IP packet */

                    return netif;
                }
            }
        }
    }
    return netif;
}
#endif
#endif /* LWIP_HOOK_IP4_ROUTE_SRC */



bool lwip_getsockopt_impl_ext(int s, struct lwip_sock* sock, int level, int optname, void *optval, socklen_t *optlen, int *err)
{
    return true;
}

#include "lwip/sockets.h"
#include "lwip/priv/sockets_priv.h"
#include "lwip/netif.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "lwip/igmp.h"
#include "lwip/mld6.h"
#include "lwip/inet.h"
#include "lwip/tcp.h"
#include "lwip/raw.h"
#include "lwip/udp.h"

#define LWIP_SOCKOPT_CHECK_OPTLEN(sock, optlen, opttype) do { if ((optlen) < sizeof(opttype)) { done_socket(sock); return EINVAL; }}while(0)
#define LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, optlen, opttype) do { \
  LWIP_SOCKOPT_CHECK_OPTLEN(sock, optlen, opttype); \
  if (((sock)->conn == NULL) || ((sock)->conn->pcb.tcp == NULL)) { done_socket(sock); return EINVAL; } }while(0)

static void
done_socket(struct lwip_sock *sock)
{
    SYS_ARCH_DECL_PROTECT(lev);
    LWIP_ASSERT("sock != NULL", sock != NULL);

    SYS_ARCH_PROTECT(lev);
    LWIP_ASSERT("sock->fd_used > 0", sock->fd_used > 0);
    sock->fd_used--;
    LWIP_ASSERT("sock->fd_used == 0", sock->fd_used == 0);
    SYS_ARCH_UNPROTECT(lev);
}

bool lwip_setsockopt_impl_ext(int s, struct lwip_sock* sock, int level, int optname, const void *optval, socklen_t optlen, int *err)
{
    switch (level) {

        /* Level: SOL_SOCKET */
        case IPPROTO_IPV6:
            switch (optname) {
#if ESP_IPV6
#if  LWIP_IPV6_MLD && LWIP_MULTICAST_TX_OPTIONS  /* Multicast options, similar to LWIP_IGMP options for IPV4 */
                case IPV6_MULTICAST_IF: /* NB: like IP_MULTICAST_IF, this returns an IP not an index */
                    LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, optlen, u8_t);
                    if (NETCONNTYPE_GROUP(netconn_type(sock->conn)) != NETCONN_UDP) {
                        return ENOPROTOOPT;
                    }
                    *(u8_t*)optval = udp_get_multicast_netif_index(sock->conn->pcb.udp);
                    LWIP_DEBUGF(SOCKETS_DEBUG, ("lwip_getsockopt(%d, IPPROTO_IPV6, IPV6_MULTICAST_IF) = 0x%"X32_F"\n",
                            s, *(u32_t *)optval));
                    break;
                case IPV6_MULTICAST_HOPS:
                    LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, optlen, u8_t);
                    if (NETCONNTYPE_GROUP(netconn_type(sock->conn)) != NETCONN_UDP) {
                        return ENOPROTOOPT;
                    }
                    *(u8_t*)optval = udp_get_multicast_ttl(sock->conn->pcb.udp);
                    LWIP_DEBUGF(SOCKETS_DEBUG, ("lwip_getsockopt(%d, IPPROTO_IPV6, IP_MULTICAST_LOOP) = %d\n",
                            s, *(int *)optval));
                    break;
                case IPV6_MULTICAST_LOOP:
                    LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, optlen, u8_t);
                    if ((udp_flags(sock->conn->pcb.udp) & UDP_FLAGS_MULTICAST_LOOP) != 0) {
                        *(u8_t*)optval = 1;
                    } else {
                        *(u8_t*)optval = 0;
                    }
                    LWIP_DEBUGF(SOCKETS_DEBUG, ("lwip_getsockopt(%d, IPPROTO_IPV6, IP_MULTICAST_LOOP) = %d\n",
                            s, *(int *)optval));
                    break;

#endif /* LWIP_IPV6_MLD && LWIP_MULTICAST_TX_OPTIONS */
#endif /* ESP_IPV6 */
            }
    }
    return true;
}
