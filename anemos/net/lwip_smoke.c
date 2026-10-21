/* lwIP smoke test: initialize NO_SYS lwIP, use the built-in 127.0.0.1
 * loopback netif, and prove a UDP datagram travels through the stack and is
 * delivered to a bound PCB. This is the smallest evidence that the pinned
 * library builds for i686 Windows and that its loopback path works without an
 * OS, before the socket-core adapter is written. */
/* windows.h first: lwIP's def.h defines ntohs/htons macros that clash with
 * the Winsock prototypes if the system header is parsed afterwards. */
#include <windows.h>

#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"

#include <stdio.h>
#include <string.h>

u32_t sys_now(void)
{
    return (u32_t)GetTickCount();
}

static int received;
static char received_text[16];

static void on_udp(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                   const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;
    if (p != NULL) {
        u16_t copy = p->tot_len < sizeof received_text - 1
                   ? p->tot_len : (u16_t)(sizeof received_text - 1);
        (void)pbuf_copy_partial(p, received_text, copy, 0);
        received_text[copy] = '\0';
        received = 1;
        pbuf_free(p);
    }
}

int main(void)
{
    struct netif *loop;
    struct udp_pcb *pcb;
    ip_addr_t dst;
    struct pbuf *p;
    const char *payload = "ping";
#ifdef ANEM_LWIP_NEGATIVE
    const char *expected = "pong";      /* deliberate gate control */
#else
    const char *expected = "ping";
#endif

    lwip_init();
    loop = NULL;
    for (struct netif *n = netif_list; n != NULL; n = n->next) {
        if (n->name[0] == 'l' && n->name[1] == 'o') {
            loop = n;
            break;
        }
    }
    if (loop == NULL || !netif_is_up(loop) || !netif_is_link_up(loop)) {
        printf("FAIL lwIP loopback netif is not up\n");
        return 1;
    }

    pcb = udp_new();
    if (pcb == NULL) {
        printf("FAIL udp_new\n");
        return 1;
    }
    IP_ADDR4(&dst, 127, 0, 0, 1);
    if (udp_bind(pcb, &dst, 5555) != ERR_OK) {
        printf("FAIL udp_bind\n");
        udp_remove(pcb);
        return 1;
    }
    udp_recv(pcb, on_udp, NULL);

    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)strlen(payload), PBUF_RAM);
    if (p == NULL || pbuf_take(p, payload, (u16_t)strlen(payload)) != ERR_OK) {
        printf("FAIL pbuf\n");
        if (p != NULL) {
            pbuf_free(p);
        }
        udp_remove(pcb);
        return 1;
    }
    if (udp_sendto(pcb, p, &dst, 5555) != ERR_OK) {
        printf("FAIL udp_sendto\n");
        pbuf_free(p);
        udp_remove(pcb);
        return 1;
    }
    pbuf_free(p);
    netif_poll(loop);
    sys_check_timeouts();

    if (!received || strcmp(received_text, expected) != 0) {
        printf("FAIL loopback datagram (received=%d text='%s')\n",
               received, received_text);
        udp_remove(pcb);
        return 1;
    }
    printf("PASS lwIP loopback UDP round-trip ('%s')\n", received_text);
    udp_remove(pcb);
    return 0;
}
