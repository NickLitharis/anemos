/* Host-side twin of xp_probe.c: answers the same case names with our socket
 * core and loopback transport. Values are compared against XP's certificate by
 * conformance_compare.py; this program never reads the XP file and never
 * asserts that it is right — XP is the oracle.
 */
#include "loopback.h"

#include <stdio.h>
#include <string.h>

static AnemLoopback lb;
static FILE *record;

static void report_u(const char *name, unsigned long value)
{
    printf("%s %lu\n", name, value);
    if (record != NULL) {
        fprintf(record, "%s %lu\n", name, value);
    }
}

static void report_s(const char *name, const char *value)
{
    printf("%s %s\n", name, value);
    if (record != NULL) {
        fprintf(record, "%s %s\n", name, value);
    }
}

static uint32_t loopback_addr(void)
{
    uint32_t addr = 0;

    (void)anem_inet_pton4("127.0.0.1", &addr);
    return addr;
}

static void fresh(void)
{
    anem_socket_reset();
    anem_loopback_init(&lb);
}

static void parse_cases(void)
{
    uint32_t lo = loopback_addr();
    char text[ANEM_INET_ADDRSTRLEN];
    uint32_t addr = 0;

    report_u("inet_addr.plain", (unsigned long)anem_inet_addr("127.0.0.1"));
    report_u("inet_addr.short", (unsigned long)anem_inet_addr("1.2.3"));
    report_u("inet_addr.hex", (unsigned long)anem_inet_addr("0x7f.0.0.1"));
    report_u("inet_addr.leadzero", (unsigned long)anem_inet_addr("1.2.3.04"));
    report_u("inet_addr.broadcast", (unsigned long)anem_inet_addr("255.255.255.255"));
    report_u("inet_addr.leadspace", (unsigned long)anem_inet_addr(" 1.2.3.4"));
    report_s("inet_ntoa.loopback", anem_inet_ntoa(lo));
    report_s("inet_ntoa.zero", anem_inet_ntoa(0));

    addr = anem_inet_addr("127.0.0.1");
    report_u("wsastring.plain.rc", addr != ANEM_INADDR_NONE ? 0u : 1u);
    report_u("wsastring.plain.value", (unsigned long)addr);
    report_u("wsastring.short.rc",
             anem_inet_addr("1.2.3") != ANEM_INADDR_NONE ? 0u : 1u);
    report_u("wsastring.leadzero.rc",
             anem_inet_addr("1.2.3.04") != ANEM_INADDR_NONE ? 0u : 1u);
    report_u("wsastring.hex.rc",
             anem_inet_addr("0x7f.0.0.1") != ANEM_INADDR_NONE ? 0u : 1u);
    (void)anem_inet_pton4("10.0.0.1", &addr);
    report_u("wsaddrtostring.ok",
             anem_inet_ntop4(addr, text, sizeof text) != NULL ? 1u : 0u);
    report_s("wsaddrtostring.value", text);
}

static int blocking_connect(int id, uint32_t addr, uint16_t port, int *error)
{
    int rc = anem_socket_connect(id, addr, port);

    anem_loopback_pump(&lb);
    if (rc < 0) {
        *error = -rc;
        return 1;
    }
    *error = anem_socket_error(id);
    return *error != 0 ? 1 : 0;
}

static void error_cases(void)
{
    uint32_t lo = loopback_addr();
    int error = 0;
    int s;
    int listener;
    int twin;
    int dgram;

    fresh();

    s = anem_socket_socket(ANEM_SOCK_STREAM);
    report_u("connect.refused.rc", (unsigned long)blocking_connect(s, lo, 1, &error));
    report_u("connect.refused.err", (unsigned long)error);
    report_u("connect.retry.rc", (unsigned long)blocking_connect(s, lo, 1, &error));
    report_u("connect.retry.err", (unsigned long)error);
    (void)anem_socket_close(s);

    s = anem_socket_socket(ANEM_SOCK_STREAM);
    report_u("bind.first.ok",
             anem_socket_bind(s, lo, 49190) == 0 ? 1u : 0u);
    twin = anem_socket_socket(ANEM_SOCK_STREAM);
    error = -anem_socket_bind(twin, lo, 49190);
    report_u("bind.conflict.rc", error != 0 ? 1u : 0u);
    report_u("bind.conflict.err", (unsigned long)error);
    dgram = anem_socket_socket(ANEM_SOCK_DGRAM);
    error = -anem_socket_bind(dgram, lo, 49190);
    report_u("bind.udp_same_port.rc", error != 0 ? 1u : 0u);
    if (error == 0) {
        error = -anem_socket_listen(dgram, 1);
        report_u("listen.dgram.rc", error != 0 ? 1u : 0u);
        report_u("listen.dgram.err", (unsigned long)error);
    }
    (void)anem_socket_close(twin);
    (void)anem_socket_close(dgram);
    (void)anem_socket_close(s);

    s = anem_socket_socket(ANEM_SOCK_STREAM);
    error = -anem_socket_send(s, "x", 1);
    report_u("send.unconnected.rc", error != 0 ? 1u : 0u);
    report_u("send.unconnected.err", (unsigned long)error);
    error = -anem_socket_shutdown(s, ANEM_SD_SEND);
    report_u("shutdown.unconnected.rc", error != 0 ? 1u : 0u);
    report_u("shutdown.unconnected.err", (unsigned long)error);
    (void)anem_socket_close(s);

    {
        int bad = 9999;
        int ready = 0;

        error = -anem_socket_select(&bad, 1, NULL, 0, &ready, NULL);
        report_u("select.bad.rc", error != 0 ? 1u : 0u);
        report_u("select.bad.err", (unsigned long)error);
    }

    listener = anem_socket_socket(ANEM_SOCK_STREAM);
    report_u("listen.ok", anem_socket_bind(listener, lo, 49191) == 0
             && anem_socket_listen(listener, 4) == 0 ? 1u : 0u);
    error = -anem_socket_connect(listener, lo, 49191);
    report_u("connect.on_listener.rc", error != 0 ? 1u : 0u);
    report_u("connect.on_listener.err", (unsigned long)error);
    (void)anem_socket_close(listener);
}

static int select_ready(int id, int for_write)
{
    int ready = 0;

    if (for_write) {
        (void)anem_socket_select(NULL, 0, &id, 1, NULL, &ready);
    } else {
        (void)anem_socket_select(&id, 1, NULL, 0, &ready, NULL);
    }
    return ready;
}

static void readiness_cases(void)
{
    uint32_t lo = loopback_addr();
    int listener;
    int client;
    int accepted = 0;
    int refused;
    int error = 0;

    fresh();
    listener = anem_socket_socket(ANEM_SOCK_STREAM);
    report_u("ready.listener.listen",
             anem_socket_bind(listener, lo, 49192) == 0
             && anem_socket_listen(listener, 4) == 0 ? 1u : 0u);
    client = anem_socket_socket(ANEM_SOCK_STREAM);
    report_u("ready.connect.ok",
             blocking_connect(client, lo, 49192, &error) == 0 ? 1u : 0u);
    report_u("ready.listener.readable", (unsigned long)select_ready(listener, 0));
    accepted = anem_socket_accept(listener);
    anem_loopback_pump(&lb);
    report_u("ready.accept.ok", accepted > 0 ? 1u : 0u);
    if (accepted > 0) {
        report_u("ready.accepted.writable", (unsigned long)select_ready(accepted, 1));
    }
    report_u("ready.shutdown.rc",
             anem_socket_shutdown(client, ANEM_SD_SEND) == 0 ? 1u : 0u);
    report_u("ready.after_send_shutdown.write",
             (unsigned long)select_ready(client, 1));
    error = -anem_socket_send(client, "x", 1);
    report_u("ready.after_send_shutdown.send_rc", error != 0 ? 1u : 0u);
    report_u("ready.after_send_shutdown.send_err", (unsigned long)error);

    refused = anem_socket_socket(ANEM_SOCK_STREAM);
    error = -anem_socket_connect(refused, lo, 1);
    /* A blocking call would wait; the non-blocking result is WOULDBLOCK. */
    report_u("ready.nonblock.connect_rc", error == 0 ? 1u : 0u);
    report_u("ready.nonblock.connect_err",
             error == 0 ? (unsigned long)ANEM_WSAEWOULDBLOCK
                        : (unsigned long)error);
    anem_loopback_pump(&lb);
    report_u("ready.nonblock.write_ready", (unsigned long)select_ready(refused, 1));
    report_u("ready.nonblock.so_error",
             (unsigned long)anem_socket_error(refused));

    (void)anem_socket_close(refused);
    (void)anem_socket_close(client);
    if (accepted > 0) {
        (void)anem_socket_close(accepted);
    }
    (void)anem_socket_close(listener);
}

int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] != '\0') {
        record = fopen(argv[1], "w");
    }
    parse_cases();
    error_cases();
    readiness_cases();
    if (record != NULL) {
        fprintf(record, "all winsock observations complete\n");
        fclose(record);
    }
    printf("all winsock observations complete\n");
    return 0;
}
