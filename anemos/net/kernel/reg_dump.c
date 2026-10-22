/* Dump a registry subtree so the host can see how XP registers a network
 * protocol component (tcpip) and replicate the shape for AnemosNet. */
#include <windows.h>
#include <stdio.h>
#include <io.h>

static FILE *out;
static int lines;

static void dump_key(HKEY parent, const char *path, int depth)
{
    HKEY key;
    DWORD index = 0;
    char name[256];
    char sub[512];

    if (depth > 5 || lines > 4000) {
        return;
    }
    if (RegOpenKeyExA(parent, path, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return;
    }
    fprintf(out, "K %s\n", path);
    for (;;) {
        DWORD name_len = sizeof name;
        DWORD type = 0;
        DWORD size = 0;
        BYTE *data;
        LONG rc = RegEnumValueA(key, index, name, &name_len, NULL, &type,
                                NULL, &size);
        if (rc != ERROR_SUCCESS) {
            if (rc == ERROR_MORE_DATA) {
                index++;
                continue;
            }
            break;
        }
        data = malloc(size + 2);
        if (data == NULL) {
            break;
        }
        if (RegQueryValueExA(key, name, NULL, &type, data, &size)
                == ERROR_SUCCESS) {
            fprintf(out, "V %s type=%lu size=%lu ", name,
                    (unsigned long)type, (unsigned long)size);
            if (type == REG_SZ || type == REG_EXPAND_SZ) {
                fprintf(out, "%s", (char *)data);
            } else if (type == REG_MULTI_SZ) {
                char *walk = (char *)data;
                while ((size_t)(walk - (char *)data) < size && *walk != '\0') {
                    fprintf(out, "[%s]", walk);
                    walk += strlen(walk) + 1;
                }
            } else {
                for (DWORD i = 0; i < size && i < 64; i++) {
                    fprintf(out, "%02x", data[i]);
                }
            }
            fprintf(out, "\n");
            lines++;
        }
        free(data);
        index++;
    }
    index = 0;
    while (RegEnumKeyExA(key, index, name, &(DWORD){sizeof name}, NULL, NULL,
                         NULL, NULL) == ERROR_SUCCESS) {
        (void)snprintf(sub, sizeof sub, "%s\\%s", path, name);
        dump_key(key, name, depth + 1);
        index++;
    }
    RegCloseKey(key);
}

int main(int argc, char **argv)
{
    const char *paths[] = {
        "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E975-E325-11CE-BFC1-08002BE10318}",
        "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Linkage",
        "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Ndi\\Interfaces",
        "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces",
        "SYSTEM\\CurrentControlSet\\Services\\PCnet\\Linkage",
        "SYSTEM\\CurrentControlSet\\Services\\PCnet\\Ndi\\Interfaces",
    };

    if (argc > 1 && argv[1][0] != '\0') {
        out = fopen(argv[1], "w");
    }
    if (out == NULL) {
        return 1;
    }
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        dump_key(HKEY_LOCAL_MACHINE, paths[i], 0);
    }
    fprintf(out, "all registry observations complete\n");
    fflush(out);
    (void)_commit(_fileno(out));
    fclose(out);
    Sleep(8000);
    (void)WinExec("shutdown -s -f -t 0", SW_HIDE);
    return 0;
}
