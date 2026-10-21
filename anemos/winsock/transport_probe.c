/* End-to-end scenarios over the loopback transport: sockets exchange real
 * bytes through the socket core, so lifecycle, readiness and the data path are
 * exercised together rather than in isolation. */
#include "loopback.h"

#include <stdio.h>
#include <string.h>

static AnemLoopback lb;
static unsigned failures;

static void check(int cond, const char *what)
{
    if (cond) {
        printf("PASS %s\n", what);
    } else {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static void fresh(void)
{
    anem_socket_reset();
    anem_loopback_init(&lb);
}

static int recv_text(int id, char *buf, unsigned cap, unsigned *got)
{
    memset(buf, 0, cap);
    return anem_loopback_recv(&lb, id, buf, cap, got);
}

static void scenario_tcp(void)
{
    static const uint32_t lo = UINT32_C(0x7f000001);
    char buf[16];
    unsigned got = 0;
    int server;
    int client;
    int accepted;

    fresh();
    server = anem_socket_socket(ANEM_SOCK_STREAM);
    check(server > 0, "tcp: server socket");
    check(anem_socket_bind(server, lo, 8080) == 0, "tcp: bind 8080");
    check(anem_socket_listen(server, 4) == 0, "tcp: listen");
    anem_loopback_pump(&lb);

    client = anem_socket_socket(ANEM_SOCK_STREAM);
    check(client > 0, "tcp: client socket");
    check(anem_socket_connect(client, lo, 8080) == 0, "tcp: connect");
    anem_loopback_pump(&lb);
    check(anem_socket_state(client) == ANEM_STATE_CONNECTED,
          "tcp: client connected");

    accepted = anem_socket_accept(server);
    check(accepted > 0, "tcp: accept returns a socket");
    anem_loopback_pump(&lb);
    check(anem_socket_state(accepted) == ANEM_STATE_CONNECTED,
          "tcp: accepted socket connected");

    check(anem_loopback_send(&lb, client, "hello", 5) == 0, "tcp: send hello");
    check(recv_text(accepted, buf, sizeof buf, &got) == 0 && got == 5
          && memcmp(buf, "hello", 5) == 0, "tcp: server receives hello");

    check(anem_loopback_send(&lb, accepted, "world", 5) == 0, "tcp: send world");
    check(recv_text(client, buf, sizeof buf, &got) == 0 && got == 5
          && memcmp(buf, "world", 5) == 0, "tcp: client receives world");

    check(anem_socket_shutdown(client, ANEM_SD_SEND) == 0, "tcp: shutdown send");
    anem_loopback_pump(&lb);
    check(recv_text(accepted, buf, sizeof buf, &got) == 0 && got == 0,
          "tcp: peer sees EOF after shutdown");

    check(anem_socket_close(accepted) == 0, "tcp: close accepted socket");
    anem_loopback_pump(&lb);
    check(recv_text(client, buf, sizeof buf, &got) == 0 && got == 0,
          "tcp: close delivers EOF to the peer");
    (void)anem_socket_close(server);
    (void)anem_socket_close(client);
}

static void scenario_refused(void)
{
    static const uint32_t lo = UINT32_C(0x7f000001);
    int client;

    fresh();
    client = anem_socket_socket(ANEM_SOCK_STREAM);
    check(anem_socket_connect(client, lo, 9999) == 0, "refused: connect issued");
    anem_loopback_pump(&lb);
    check(anem_socket_error(client) == ANEM_WSAECONNREFUSED,
          "refused: WSAECONNREFUSED is reported");
    check(anem_socket_state(client) == ANEM_STATE_BOUND,
          "refused: socket returns to bound");
    (void)anem_socket_close(client);
}

static void scenario_backlog(void)
{
    static const uint32_t lo = UINT32_C(0x7f000001);
    char buf[8];
    unsigned got = 0;
    int server;
    int one;
    int two;

    fresh();
    server = anem_socket_socket(ANEM_SOCK_STREAM);
    (void)anem_socket_bind(server, lo, 8081);
    (void)anem_socket_listen(server, 1);
    anem_loopback_pump(&lb);

    one = anem_socket_socket(ANEM_SOCK_STREAM);
    (void)anem_socket_connect(one, lo, 8081);
    anem_loopback_pump(&lb);
    two = anem_socket_socket(ANEM_SOCK_STREAM);
    (void)anem_socket_connect(two, lo, 8081);
    anem_loopback_pump(&lb);

    check(anem_socket_state(one) == ANEM_STATE_CONNECTED,
          "backlog: first connection is queued");
    check(recv_text(two, buf, sizeof buf, &got) == -ANEM_WSAECONNRESET,
          "backlog: overflow resets the second connection");
    check(anem_socket_accept(server) > 0, "backlog: first connection accepted");
    anem_loopback_pump(&lb);
    (void)anem_socket_close(one);
    (void)anem_socket_close(two);
    (void)anem_socket_close(server);
}

static void scenario_udp(void)
{
    static const uint32_t lo = UINT32_C(0x7f000001);
    char buf[16];
    unsigned got = 0;
    int a;
    int b;

    fresh();
    a = anem_socket_socket(ANEM_SOCK_DGRAM);
    b = anem_socket_socket(ANEM_SOCK_DGRAM);
    check(a > 0 && b > 0, "udp: two sockets");
    (void)anem_socket_bind(a, lo, 5000);
    (void)anem_socket_bind(b, lo, 5001);
    (void)anem_socket_connect(a, lo, 5001);
    (void)anem_socket_connect(b, lo, 5000);
    anem_loopback_pump(&lb);

    check(anem_loopback_send(&lb, a, "ping", 4) == 0, "udp: send datagram");
    check(recv_text(b, buf, sizeof buf, &got) == 0 && got == 4
          && memcmp(buf, "ping", 4) == 0, "udp: peer receives datagram");
    check(anem_loopback_send(&lb, b, "pong", 4) == 0, "udp: reply datagram");
    check(recv_text(a, buf, sizeof buf, &got) == 0 && got == 4
          && memcmp(buf, "pong", 4) == 0, "udp: reply arrives");
    (void)anem_socket_close(a);
    (void)anem_socket_close(b);
}

int main(void)
{
    scenario_tcp();
    scenario_refused();
    scenario_backlog();
    scenario_udp();

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all loopback transport checks pass\n");
    return 0;
}
