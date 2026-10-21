/* Trace harness for the socket core: tab-separated operations in, one
 * canonical result line per operation plus the requests it emitted. The
 * Python model consumes the same trace and must produce the same text. */
#include "socket_core.h"

#include <stdio.h>
#include <string.h>

#define MAX_FIELDS 5

static void print_requests(void)
{
    AnemSockRequest request;

    while (anem_socket_take_request(&request) != 0) {
        switch (request.kind) {
        case ANEM_REQUEST_OPEN:
            printf("req open %d %u\n", request.socket, request.value);
            break;
        case ANEM_REQUEST_BIND:
            printf("req bind %d %s %u\n", request.socket,
                   anem_inet_ntoa(request.addr), request.port);
            break;
        case ANEM_REQUEST_LISTEN:
            printf("req listen %d %u\n", request.socket, request.value);
            break;
        case ANEM_REQUEST_CONNECT:
            printf("req connect %d %s %u\n", request.socket,
                   anem_inet_ntoa(request.addr), request.port);
            break;
        case ANEM_REQUEST_SHUTDOWN:
            printf("req shutdown %d %u\n", request.socket, request.value);
            break;
        case ANEM_REQUEST_CLOSE:
            printf("req close %d\n", request.socket);
            break;
        case ANEM_REQUEST_REFUSE:
            printf("req refuse %d\n", request.socket);
            break;
        case ANEM_REQUEST_SEND:
            printf("req send %d %u\n", request.socket, request.value);
            break;
        }
    }
}

/* Strict: optional '-', then decimal digits, nothing else. strtol's prefix
 * parsing would accept "1x" as 1 and silently diverge from the model. */
static int parse_id(const char *text)
{
    const char *p = text;
    int sign = 1;
    long value = 0;

    if (*p == '-') {
        sign = -1;
        p++;
    }
    if (*p < '0' || *p > '9') {
        return -1;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        if (value > 1000000) {
            return -1;
        }
        p++;
    }
    if (*p != '\0') {
        return -1;
    }
    return (int)(sign * value);
}

static unsigned parse_unsigned(const char *text, unsigned *out)
{
    unsigned long value;

    value = 0;
    if (text[0] == '\0') {
        return 0;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        value = value * 10ul + (unsigned long)(*text - '0');
        if (value > 65535ul) {
            return 0;
        }
    }
    *out = (unsigned)value;
    return 1;
}

/* Comma-separated ids, strict decimal. Returns the number parsed; a malformed
 * list stops at the bad field, which the caller treats as an id error. */
static int parse_id_list(const char *text, int *ids, unsigned max)
{
    int count = 0;
    const char *p = text;

    if (*p == '\0') {
        return 0;
    }
    while (*p != '\0') {
        int value = 0;

        if (count >= (int)max || *p < '0' || *p > '9') {
            return -1;
        }
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (*p - '0');
            if (value > 1000000) {
                return -1;
            }
            p++;
        }
        ids[count++] = value;
        if (*p == ',') {
            p++;
            if (*p == '\0') {
                return -1;
            }
        } else if (*p != '\0') {
            return -1;
        }
    }
    return count;
}

static void print_bits(const int *ready, unsigned count)
{
    if (count == 0u) {
        printf("-");
        return;
    }
    for (unsigned i = 0; i < count; i++) {
        printf("%d", ready[i] != 0 ? 1 : 0);
    }
}

static AnemSockType parse_type(const char *text, int *valid)
{
    *valid = 1;
    if (strcmp(text, "stream") == 0) {
        return ANEM_SOCK_STREAM;
    }
    if (strcmp(text, "dgram") == 0) {
        return ANEM_SOCK_DGRAM;
    }
    *valid = 0;
    return ANEM_SOCK_STREAM;
}

int main(void)
{
    char line[1024];

    anem_socket_reset();
    while (fgets(line, sizeof line, stdin) != NULL) {
        char *fields[MAX_FIELDS] = { 0 };
        unsigned count = 0;
        char *p = line;
        size_t len = strlen(line);
        int valid = 1;

        if (len == sizeof line - 1u && line[len - 1] != '\n') {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
            printf("? overlong\n");
            continue;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        fields[count++] = p;
        for (; *p != '\0' && count < MAX_FIELDS; p++) {
            if (*p == '\t') {
                *p = '\0';
                fields[count++] = p + 1;
            }
        }
        if (len == 0) {
            continue;
        }
        if (strcmp(fields[0], "socket") == 0 && count >= 2) {
            AnemSockType type = parse_type(fields[1], &valid);
            int id = valid ? anem_socket_socket(type) : -ANEM_WSAEINVAL;
            printf("socket %s %d\n", fields[1], id);
        } else if (strcmp(fields[0], "bind") == 0 && count >= 4) {
            uint32_t addr = 0;
            unsigned port = 0;
            int rc = -ANEM_WSAEINVAL;
            if (anem_inet_pton4(fields[2], &addr) && parse_unsigned(fields[3], &port)) {
                rc = anem_socket_bind(parse_id(fields[1]), addr, (uint16_t)port);
            }
            printf("bind %s %s %s %d\n", fields[1], fields[2], fields[3], rc);
        } else if (strcmp(fields[0], "listen") == 0 && count >= 3) {
            unsigned backlog = 0;
            int rc = -ANEM_WSAEINVAL;
            if (parse_unsigned(fields[2], &backlog)) {
                rc = anem_socket_listen(parse_id(fields[1]), backlog);
            }
            printf("listen %s %s %d\n", fields[1], fields[2], rc);
        } else if (strcmp(fields[0], "connect") == 0 && count >= 4) {
            uint32_t addr = 0;
            unsigned port = 0;
            int rc = -ANEM_WSAEINVAL;
            if (anem_inet_pton4(fields[2], &addr) && parse_unsigned(fields[3], &port)) {
                rc = anem_socket_connect(parse_id(fields[1]), addr, (uint16_t)port);
            }
            printf("connect %s %s %s %d\n", fields[1], fields[2], fields[3], rc);
        } else if (strcmp(fields[0], "accept") == 0 && count >= 2) {
            printf("accept %s %d\n", fields[1], anem_socket_accept(parse_id(fields[1])));
        } else if (strcmp(fields[0], "shutdown") == 0 && count >= 3) {
            unsigned how = 0;
            int rc = -ANEM_WSAEINVAL;
            if (parse_unsigned(fields[2], &how)) {
                rc = anem_socket_shutdown(parse_id(fields[1]), how);
            }
            printf("shutdown %s %s %d\n", fields[1], fields[2], rc);
        } else if (strcmp(fields[0], "close") == 0 && count >= 2) {
            printf("close %s %d\n", fields[1], anem_socket_close(parse_id(fields[1])));
        } else if (strcmp(fields[0], "state") == 0 && count >= 2) {
            printf("state %s %s\n", fields[1],
                   anem_socket_state_name(anem_socket_state(parse_id(fields[1]))));
        } else if (strcmp(fields[0], "connected") == 0 && count >= 2) {
            anem_socket_event_connected(parse_id(fields[1]));
            printf("connected %s\n", fields[1]);
        } else if (strcmp(fields[0], "refused") == 0 && count >= 2) {
            anem_socket_event_refused(parse_id(fields[1]));
            printf("refused %s\n", fields[1]);
        } else if (strcmp(fields[0], "incoming") == 0 && count >= 2) {
            anem_socket_event_incoming(parse_id(fields[1]));
            printf("incoming %s\n", fields[1]);
        } else if (strcmp(fields[0], "send") == 0 && count >= 3) {
            unsigned length = 0;
            int rc = -ANEM_WSAEINVAL;
            if (parse_unsigned(fields[2], &length)) {
                rc = anem_socket_send(parse_id(fields[1]), length);
            }
            printf("send %s %s %d\n", fields[1], fields[2], rc);
        } else if (strcmp(fields[0], "recv") == 0 && count >= 3) {
            unsigned length = 0;
            unsigned received = 0;
            int rc = -ANEM_WSAEINVAL;
            if (parse_unsigned(fields[2], &length)) {
                rc = anem_socket_recv(parse_id(fields[1]), length, &received);
            }
            printf("recv %s %s %d %u\n", fields[1], fields[2], rc, received);
        } else if (strcmp(fields[0], "readable") == 0 && count >= 3) {
            unsigned bytes = 0;
            if (parse_unsigned(fields[2], &bytes)) {
                anem_socket_event_readable(parse_id(fields[1]), bytes);
            }
            printf("readable %s %s\n", fields[1], fields[2]);
        } else if (strcmp(fields[0], "writable") == 0 && count >= 2) {
            anem_socket_event_writable(parse_id(fields[1]));
            printf("writable %s\n", fields[1]);
        } else if (strcmp(fields[0], "eof") == 0 && count >= 2) {
            anem_socket_event_eof(parse_id(fields[1]));
            printf("eof %s\n", fields[1]);
        } else if (strcmp(fields[0], "reset") == 0 && count >= 2) {
            anem_socket_event_reset(parse_id(fields[1]));
            printf("reset %s\n", fields[1]);
        } else if (strcmp(fields[0], "error") == 0 && count >= 2) {
            printf("error %s %d\n", fields[1], anem_socket_error(parse_id(fields[1])));
        } else if (strcmp(fields[0], "select") == 0 && count >= 2) {
            int read_ids[ANEM_SOCKET_MAX];
            int write_ids[ANEM_SOCKET_MAX];
            int read_ready[ANEM_SOCKET_MAX];
            int write_ready[ANEM_SOCKET_MAX];
            int nread = parse_id_list(fields[1], read_ids, ANEM_SOCKET_MAX);
            int nwrite = (count >= 3)
                ? parse_id_list(fields[2], write_ids, ANEM_SOCKET_MAX) : 0;
            int rc;
            if (nread < 0 || nwrite < 0) {
                rc = -ANEM_WSAEINVAL;
            } else {
                rc = anem_socket_select(read_ids, (unsigned)nread,
                                        write_ids, (unsigned)nwrite,
                                        read_ready, write_ready);
            }
            printf("select %d", rc);
            if (rc >= 0) {
                printf(" ");
                print_bits(read_ready, (unsigned)nread);
                printf(" ");
                print_bits(write_ready, (unsigned)nwrite);
            }
            printf("\n");
        } else {
            printf("? %s\n", fields[0]);
        }
        print_requests();
        if (anem_socket_request_overflow() != 0) {
            printf("overflow\n");
        }
    }
    return 0;
}
