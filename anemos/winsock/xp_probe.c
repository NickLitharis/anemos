/* In-guest Winsock observation probe (P4.6).
 *
 * Runs the same scripted observations our implementation answers on the host
 * and writes one `case value` line per observation, so XP itself is the
 * oracle for the deferred questions: legacy inet_addr parsing, which connect
 * failures are retryable, TCP/UDP port namespaces, readiness after shutdown,
 * and the error codes at each edge.
 *
 * No external network: every connection is to 127.0.0.1 or an unbound port.
 * The certificate is written to argv[1] (default winsock-cert.txt) and also
 * printed, exactly like the SPI harness.
 *
 * usage: xp_probe.exe [result-file]
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

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

static void report_err(const char *name)
{
    report_u(name, (unsigned long)WSAGetLastError());
}

static int set_nonblocking(SOCKET s)
{
    u_long mode = 1;

    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

static unsigned short bound_port(SOCKET s)
{
    struct sockaddr_in addr;
    int len = (int)sizeof addr;

    memset(&addr, 0, sizeof addr);
    if (getsockname(s, (struct sockaddr *)&addr, &len) != 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

static SOCKET make_bound(int type, int protocol, unsigned short *port)
{
    SOCKET s = socket(AF_INET, type, protocol);
    struct sockaddr_in addr;

    if (s == INVALID_SOCKET) {
        return s;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    if (port != NULL) {
        *port = bound_port(s);
    }
    return s;
}

static void parse_cases(void)
{
    struct in_addr a;
    char text[64];

    report_u("inet_addr.plain", (unsigned long)inet_addr("127.0.0.1"));
    report_u("inet_addr.short", (unsigned long)inet_addr("1.2.3"));
    report_u("inet_addr.hex", (unsigned long)inet_addr("0x7f.0.0.1"));
    report_u("inet_addr.leadzero", (unsigned long)inet_addr("1.2.3.04"));
    report_u("inet_addr.broadcast", (unsigned long)inet_addr("255.255.255.255"));
    report_u("inet_addr.leadspace", (unsigned long)inet_addr(" 1.2.3.4"));
    a.s_addr = inet_addr("127.0.0.1");
    report_s("inet_ntoa.loopback", inet_ntoa(a));
    a.s_addr = 0;
    report_s("inet_ntoa.zero", inet_ntoa(a));

    /* XP's ws2_32 has no inet_pton/inet_ntop (they arrive with Vista); the
     * XP-observable equivalents are WSAStringToAddress / WSAAddressToString. */
    {
        struct sockaddr_in parsed;
        int parsed_len = (int)sizeof parsed;

        memset(&parsed, 0, sizeof parsed);
        report_u("wsastring.plain.rc",
                 WSAStringToAddressA("127.0.0.1", AF_INET, NULL,
                                     (struct sockaddr *)&parsed,
                                     &parsed_len) == 0 ? 0u : 1u);
        report_u("wsastring.plain.value", (unsigned long)parsed.sin_addr.s_addr);
    }
    {
        struct sockaddr_in parsed;
        int parsed_len = (int)sizeof parsed;

        memset(&parsed, 0, sizeof parsed);
        report_u("wsastring.short.rc",
                 WSAStringToAddressA("1.2.3", AF_INET, NULL,
                                     (struct sockaddr *)&parsed,
                                     &parsed_len) == 0 ? 0u : 1u);
    }
    {
        struct sockaddr_in parsed;
        int parsed_len = (int)sizeof parsed;

        memset(&parsed, 0, sizeof parsed);
        report_u("wsastring.leadzero.rc",
                 WSAStringToAddressA("1.2.3.04", AF_INET, NULL,
                                     (struct sockaddr *)&parsed,
                                     &parsed_len) == 0 ? 0u : 1u);
    }
    {
        struct sockaddr_in parsed;
        int parsed_len = (int)sizeof parsed;

        memset(&parsed, 0, sizeof parsed);
        report_u("wsastring.hex.rc",
                 WSAStringToAddressA("0x7f.0.0.1", AF_INET, NULL,
                                     (struct sockaddr *)&parsed,
                                     &parsed_len) == 0 ? 0u : 1u);
    }
    memset(&a, 0, sizeof a);
    a.s_addr = inet_addr("10.0.0.1");
    {
        struct sockaddr_in addr;
        DWORD text_len = (DWORD)sizeof text;

        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_addr = a;
        memset(text, 0, sizeof text);
        report_u("wsaddrtostring.ok",
                 WSAAddressToStringA((struct sockaddr *)&addr, (DWORD)sizeof addr,
                                     NULL, text, &text_len) == 0 ? 1u : 0u);
        report_s("wsaddrtostring.value", text);
    }
}

static void error_cases(void)
{
    struct sockaddr_in dst;
    struct sockaddr_in *p = &dst;
    SOCKET s;
    SOCKET listener;
    SOCKET dgram;
    unsigned short port = 0;
    unsigned short tcp_port = 0;
    int rc;

    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    dst.sin_port = htons(1);

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    rc = connect(s, (struct sockaddr *)p, (int)sizeof dst);
    report_u("connect.refused.rc", rc == SOCKET_ERROR ? 1u : 0u);
    report_err("connect.refused.err");
    rc = connect(s, (struct sockaddr *)p, (int)sizeof dst);
    report_u("connect.retry.rc", rc == SOCKET_ERROR ? 1u : 0u);
    report_err("connect.retry.err");
    closesocket(s);

    s = make_bound(SOCK_STREAM, IPPROTO_TCP, &tcp_port);
    report_u("bind.first.ok", s != INVALID_SOCKET ? 1u : 0u);
    if (s != INVALID_SOCKET) {
        SOCKET twin = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr;

        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(tcp_port);
        rc = bind(twin, (struct sockaddr *)&addr, (int)sizeof addr);
        report_u("bind.conflict.rc", rc == SOCKET_ERROR ? 1u : 0u);
        report_err("bind.conflict.err");

        dgram = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        rc = bind(dgram, (struct sockaddr *)&addr, (int)sizeof addr);
        report_u("bind.udp_same_port.rc", rc == SOCKET_ERROR ? 1u : 0u);
        if (rc == 0) {
            report_u("listen.dgram.rc", listen(dgram, 1) == SOCKET_ERROR ? 1u : 0u);
            report_err("listen.dgram.err");
            closesocket(dgram);
        }
        closesocket(twin);
    }
    closesocket(s);

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    report_u("send.unconnected.rc",
             send(s, "x", 1, 0) == SOCKET_ERROR ? 1u : 0u);
    report_err("send.unconnected.err");
    report_u("shutdown.unconnected.rc",
             shutdown(s, SD_SEND) == SOCKET_ERROR ? 1u : 0u);
    report_err("shutdown.unconnected.err");
    closesocket(s);

    {
        fd_set set;
        struct timeval tv;
        SOCKET bad = (SOCKET)9999;

        FD_ZERO(&set);
        FD_SET(bad, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        rc = select(0, &set, NULL, NULL, &tv);
        report_u("select.bad.rc", rc == SOCKET_ERROR ? 1u : 0u);
        report_err("select.bad.err");
    }

    listener = make_bound(SOCK_STREAM, IPPROTO_TCP, &tcp_port);
    report_u("listen.ok", listener != INVALID_SOCKET
             && listen(listener, 4) == 0 ? 1u : 0u);
    rc = connect(listener, (struct sockaddr *)p, (int)sizeof dst);
    report_u("connect.on_listener.rc", rc == SOCKET_ERROR ? 1u : 0u);
    report_err("connect.on_listener.err");
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }

    (void)port;
}

static void readiness_cases(void)
{
    SOCKET listener;
    SOCKET client;
    SOCKET accepted = INVALID_SOCKET;
    SOCKET refused;
    struct sockaddr_in dst;
    struct sockaddr_in local;
    unsigned short port = 0;
    fd_set set;
    struct timeval tv;
    int rc;
    int err = 0;
    int len = (int)sizeof err;

    listener = make_bound(SOCK_STREAM, IPPROTO_TCP, &port);
    report_u("ready.listener.listen", listener != INVALID_SOCKET
             && listen(listener, 4) == 0 ? 1u : 0u);

    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    dst.sin_port = htons(port);

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    rc = connect(client, (struct sockaddr *)&dst, (int)sizeof dst);
    report_u("ready.connect.ok", rc == 0 ? 1u : 0u);
    if (rc == 0) {
        FD_ZERO(&set);
        FD_SET(listener, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        report_u("ready.listener.readable",
                 select(0, &set, NULL, NULL, &tv) == 1 ? 1u : 0u);
        accepted = accept(listener, NULL, NULL);
        report_u("ready.accept.ok", accepted != INVALID_SOCKET ? 1u : 0u);
        if (accepted != INVALID_SOCKET) {
            FD_ZERO(&set);
            FD_SET(accepted, &set);
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            report_u("ready.accepted.writable",
                     select(0, NULL, &set, NULL, &tv) == 1 ? 1u : 0u);
        }
    }

    if (client != INVALID_SOCKET && accepted != INVALID_SOCKET) {
        report_u("ready.shutdown.rc", shutdown(client, SD_SEND) == 0 ? 1u : 0u);
        FD_ZERO(&set);
        FD_SET(client, &set);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        report_u("ready.after_send_shutdown.write",
                 select(0, NULL, &set, NULL, &tv) == 1 ? 1u : 0u);
        report_u("ready.after_send_shutdown.send_rc",
                 send(client, "x", 1, 0) == SOCKET_ERROR ? 1u : 0u);
        report_err("ready.after_send_shutdown.send_err");
    }

    /* Non-blocking connect to an unbound port: select must report writable
     * and SO_ERROR must carry the refusal. */
    refused = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (refused != INVALID_SOCKET && set_nonblocking(refused)) {
        memset(&local, 0, sizeof local);
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = inet_addr("127.0.0.1");
        local.sin_port = htons(1);
        rc = connect(refused, (struct sockaddr *)&local, (int)sizeof local);
        report_u("ready.nonblock.connect_rc", rc == SOCKET_ERROR ? 1u : 0u);
        report_err("ready.nonblock.connect_err");
        FD_ZERO(&set);
        FD_SET(refused, &set);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        report_u("ready.nonblock.write_ready",
                 select(0, NULL, &set, NULL, &tv) == 1 ? 1u : 0u);
        if (getsockopt(refused, SOL_SOCKET, SO_ERROR, (char *)&err,
                       &len) == 0) {
            report_u("ready.nonblock.so_error", (unsigned long)err);
        } else {
            report_err("ready.nonblock.so_error_err");
        }
    }

    if (accepted != INVALID_SOCKET) {
        closesocket(accepted);
    }
    if (client != INVALID_SOCKET) {
        closesocket(client);
    }
    if (refused != INVALID_SOCKET) {
        closesocket(refused);
    }
    if (listener != INVALID_SOCKET) {
        closesocket(listener);
    }
}

int main(int argc, char **argv)
{
    WSADATA data;

    if (argc > 1 && argv[1][0] != '\0') {
        record = fopen(argv[1], "w");
    }
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        report_err("wsa.startup_err");
        if (record != NULL) {
            fclose(record);
        }
        return 1;
    }
    parse_cases();
    error_cases();
    readiness_cases();
    WSACleanup();
    if (record != NULL) {
        fprintf(record, "all winsock observations complete\n");
        fclose(record);
    }
    printf("all winsock observations complete\n");
    return 0;
}
