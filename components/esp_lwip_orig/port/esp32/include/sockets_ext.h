#pragma once
#define IPV6_MULTICAST_IF    0x300
#define IPV6_MULTICAST_HOPS  0x301
#define IPV6_MULTICAST_LOOP  0x302

struct lwip_sock;

bool lwip_setsockopt_impl_ext(struct lwip_sock* sock, int level, int optname, const void *optval, uint32_t optlen, int *err);
bool lwip_getsockopt_impl_ext(struct lwip_sock* sock, int level, int optname, void *optval, uint32_t *optlen, int *err);