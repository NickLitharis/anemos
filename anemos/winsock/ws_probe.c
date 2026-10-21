/* Corpus probe for the Winsock address layer: one command per input line,
 * tab-separated, so leading/trailing spaces survive. Output is a stable
 * canonical line the Python oracle reproduces independently.
 *
 * usage: ws_probe.exe selftest
 *        ws_probe.exe < cases.txt > got.txt
 */
#include "winsock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static uint32_t parse_hex32(const char *text)
{
    return (uint32_t)strtoul(text, NULL, 16);
}

static int selftest(void)
{
    char buf[ANEM_INET_ADDRSTRLEN];
    char small[8];
    AnemSockAddrIn sa;
    uint32_t address = 0;

    check(anem_htonl(UINT32_C(0x01020304)) == UINT32_C(0x04030201),
          "htonl reverses on a little-endian host");
    check(anem_ntohl(anem_htonl(UINT32_C(0xdeadbeef))) == UINT32_C(0xdeadbeef),
          "ntohl inverts htonl");
    check(anem_htons(UINT16_C(0x0102)) == UINT16_C(0x0201),
          "htons reverses on a little-endian host");
    check(anem_ntohs(anem_htons(UINT16_C(0xabcd))) == UINT16_C(0xabcd),
          "ntohs inverts htons");

    check(anem_inet_pton4("127.0.0.1", &address) == 1
          && address == UINT32_C(0x7f000001), "pton parses 127.0.0.1");
    check(anem_inet_pton4("0.0.0.0", &address) == 1 && address == 0,
          "pton parses 0.0.0.0");
    check(anem_inet_pton4("255.255.255.255", &address) == 1
          && address == UINT32_C(0xffffffff), "pton parses 255.255.255.255");
    check(anem_inet_pton4("256.0.0.1", &address) == 0, "pton rejects 256");
    check(anem_inet_pton4("1.2.3", &address) == 0, "pton rejects three fields");
    check(anem_inet_pton4("1.2.3.04", &address) == 0,
          "pton rejects a leading zero");

    check(anem_inet_ntop4(UINT32_C(0xc0a80101), buf, sizeof buf) != NULL
          && strcmp(buf, "192.168.1.1") == 0, "ntop formats 192.168.1.1");
    check(anem_inet_ntop4(0, small, sizeof small) == NULL,
          "ntop rejects a short buffer");
    check(anem_inet_ntop4(0, buf, sizeof buf) != NULL
          && strcmp(buf, "0.0.0.0") == 0, "ntop formats 0.0.0.0");

    check(anem_inet_addr("10.0.0.1") == UINT32_C(0x0a000001),
          "inet_addr returns network order");
    check(anem_inet_addr("10.0.0") == ANEM_INADDR_NONE,
          "inet_addr returns INADDR_NONE on malformed input");
    check(anem_inet_addr("255.255.255.255") == ANEM_INADDR_NONE,
          "inet_addr is ambiguous for the broadcast address");
    check(strcmp(anem_inet_ntoa(UINT32_C(0x7f000001)), "127.0.0.1") == 0,
          "inet_ntoa formats via the static buffer");

    anem_sockaddr_in_init(&sa, UINT32_C(0x7f000001), 80);
    check(sa.family == ANEM_AF_INET, "sockaddr_in family is AF_INET (host)");
    check(sa.port == UINT16_C(0x5000), "sockaddr_in port is network order");
    check(sa.addr == UINT32_C(0x7f000001), "sockaddr_in address is network order");
    check(sa.zero[0] == 0 && sa.zero[7] == 0, "sockaddr_in zero padding is cleared");

    if (failures != 0u) {
        printf("FAILED: %u check(s)\n", failures);
        return 1;
    }
    printf("all Winsock address self-tests pass\n");
    return 0;
}

static void run_line(char *line)
{
    char *tab = strchr(line, '\t');
    char *command;
    char *argument;
    uint32_t address = 0;
    char text[ANEM_INET_ADDRSTRLEN];
    int ok;

    if (tab == NULL) {
        return;
    }
    *tab = '\0';
    command = line;
    argument = tab + 1;

    if (strcmp(command, "pton") == 0) {
        ok = anem_inet_pton4(argument, &address);
        printf("pton\t%s\t%d", argument, ok);
        if (ok) {
            printf("\t%08lx", (unsigned long)address);
        }
        printf("\n");
    } else if (strcmp(command, "addr") == 0) {
        printf("addr\t%s\t%08lx\n", argument,
               (unsigned long)anem_inet_addr(argument));
    } else if (strcmp(command, "ntop") == 0) {
        address = parse_hex32(argument);
        ok = anem_inet_ntop4(address, text, sizeof text) != NULL;
        printf("ntop\t%s\t%d", argument, ok);
        if (ok) {
            printf("\t%s", text);
        }
        printf("\n");
    } else if (strcmp(command, "ntoa") == 0) {
        address = parse_hex32(argument);
        printf("ntoa\t%s\t%s\n", argument, anem_inet_ntoa(address));
    }
}

int main(int argc, char **argv)
{
    char line[512];

    if (argc > 1 && strcmp(argv[1], "selftest") == 0) {
        return selftest();
    }
    while (fgets(line, sizeof line, stdin) != NULL) {
        size_t len = strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        run_line(line);
    }
    return 0;
}
