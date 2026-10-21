/* lwIP options for the kernel driver: NO_SYS, IPv4 TCP/UDP, loopif. */
#ifndef ANEM_KERNEL_LWIPOPTS_H
#define ANEM_KERNEL_LWIPOPTS_H

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 0
#define LWIP_ETHERNET 0
#define LWIP_ICMP 1
#define LWIP_RAW 0
#define LWIP_DNS 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_TCP_KEEPALIVE 0
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_HAVE_LOOPIF 1
#define LWIP_SINGLE_NETIF 0
#define LWIP_LOOPIF_MULTICAST 0
#define LWIP_NETIF_API 0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define MEM_LIBC_MALLOC 0
#define MEMP_MEM_MALLOC 0
#define MEM_SIZE (32 * 1024)
#define PBUF_POOL_SIZE 16
#define PBUF_POOL_BUFSIZE 1536
#define TCP_MSS 1460
#define TCP_SND_BUF (2 * TCP_MSS)
#define TCP_WND (2 * TCP_MSS)
#define TCP_QUEUE_OOSEQ 1
#define LWIP_STATS 0

#endif
