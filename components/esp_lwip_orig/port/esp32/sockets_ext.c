//
// Created by david on 19.01.22.
//

#include "lwip/sockets.h"
#include "lwip/priv/sockets_priv.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/raw.h"
#include "lwip/udp.h"

#define LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, optlen, opttype) do { \
  if (((optlen) < sizeof(opttype)) || ((sock)->conn == NULL) || ((sock)->conn->pcb.tcp == NULL)) { *err=EINVAL; goto exit; } }while(0)

#define LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB_TYPE(sock, optlen, opttype, netconntype) do { \
  LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, optlen, opttype); \
  if (NETCONNTYPE_GROUP(netconn_type((sock)->conn)) != netconntype) { *err=ENOPROTOOPT; goto exit; } } while(0)


bool lwip_setsockopt_impl_ext(struct lwip_sock* sock, int level, int optname, const void *optval, socklen_t optlen, int *err)
{
    if (level != IPPROTO_IPV6) {
        return false;
    }

    switch (optname) {
        default:
            return false;
#if ESP_IPV6
        case IPV6_MULTICAST_IF: /* NB: like IP_MULTICAST_IF, this takes an IP not an index */
        {
            LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB_TYPE(sock, optlen, u8_t, NETCONN_UDP);
            udp_set_multicast_netif_index(sock->conn->pcb.udp, (u8_t)(*(const u8_t*)optval));
        }
            break;
        case IPV6_MULTICAST_HOPS:
            LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB_TYPE(sock, optlen, u8_t, NETCONN_UDP);
            udp_set_multicast_ttl(sock->conn->pcb.udp, (u8_t)(*(const u8_t*)optval));
            break;
        case IPV6_MULTICAST_LOOP:
            LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB_TYPE(sock, optlen, u8_t, NETCONN_UDP);
            if (*(const u8_t*)optval) {
                udp_setflags(sock->conn->pcb.udp, udp_flags(sock->conn->pcb.udp) | UDP_FLAGS_MULTICAST_LOOP);
            } else {
                udp_setflags(sock->conn->pcb.udp, udp_flags(sock->conn->pcb.udp) & ~UDP_FLAGS_MULTICAST_LOOP);
            }
            break;
#endif/* ESP_IPV6 */
    }
exit:
    return true;
}

bool lwip_getsockopt_impl_ext(struct lwip_sock* sock, int level, int optname, void *optval, uint32_t *optlen, int *err)
{
    if (level != IPPROTO_IPV6) {
        return false;
    }

    switch (optname) {
        default:
            return false;
        case IPV6_MULTICAST_IF: /* NB: like IP_MULTICAST_IF, this returns an IP not an index */
            LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, *optlen, u8_t);
            if (NETCONNTYPE_GROUP(netconn_type(sock->conn)) != NETCONN_UDP) {
                *err = ENOPROTOOPT;
                goto exit;
            }
            *(u8_t*)optval = udp_get_multicast_netif_index(sock->conn->pcb.udp);
            LWIP_DEBUGF(SOCKETS_DEBUG, ("lwip_getsockopt(%d, IPPROTO_IPV6, IPV6_MULTICAST_IF) = 0x%"X32_F"\n",
                    s, *(u32_t *)optval));
            break;
        case IPV6_MULTICAST_HOPS:
            printf("IPV6_MULTICAST_HOPS\n");
            LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, *optlen, u8_t);
            if (NETCONNTYPE_GROUP(netconn_type(sock->conn)) != NETCONN_UDP) {
                *err = ENOPROTOOPT;
                goto exit;
            }
            *(u8_t*)optval = udp_get_multicast_ttl(sock->conn->pcb.udp);
            LWIP_DEBUGF(SOCKETS_DEBUG, ("lwip_getsockopt(%d, IPPROTO_IPV6, IP_MULTICAST_LOOP) = %d\n",
                    s, *(int *)optval));
            break;
        case IPV6_MULTICAST_LOOP:
            LWIP_SOCKOPT_CHECK_OPTLEN_CONN_PCB(sock, *optlen, u8_t);
            if ((udp_flags(sock->conn->pcb.udp) & UDP_FLAGS_MULTICAST_LOOP) != 0) {
                *(u8_t*)optval = 1;
            } else {
                *(u8_t*)optval = 0;
            }
            LWIP_DEBUGF(SOCKETS_DEBUG, ("lwip_getsockopt(%d, IPPROTO_IPV6, IP_MULTICAST_LOOP) = %d\n",
                    s, *(int *)optval));
            break;
    }
    exit:
    return true;
}