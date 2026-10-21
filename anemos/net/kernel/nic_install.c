/* K3 prerequisite: give XP the NIC driver it already ships.
 *
 * The VM emulates an Intel e1000. XP's driver.cab contains e1000nt5.sys and
 * Windows\inf contains nete1000.inf, but the image never installed them, so
 * NDIS has no miniport to bind. This test extracts the .sys with the guest's
 * own expand.exe, finds the driverless PCI device, and installs the INF with
 * UpdateDriverForPlugAndPlayDevices. No Microsoft binary enters the repo.
 */
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <io.h>

static FILE *record;

static void report(const char *name, unsigned long value)
{
    printf("%s %lu\n", name, value);
    if (record != NULL) {
        fprintf(record, "%s %lu\n", name, value);
    }
}

static void report_str(const char *name, const char *value)
{
    printf("%s %s\n", name, value == NULL ? "(null)" : value);
    if (record != NULL) {
        fprintf(record, "%s %s\n", name, value == NULL ? "(null)" : value);
    }
}

static int install_network_device(void)
{
    HDEVINFO devices;
    SP_DEVINFO_DATA data;
    DWORD index = 0;
    int installed = 0;

    devices = SetupDiGetClassDevsA(NULL, "PCI", NULL,
                                   DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devices == INVALID_HANDLE_VALUE) {
        report("setupdi.err", (unsigned long)GetLastError());
        return 0;
    }
    data.cbSize = sizeof data;
    while (SetupDiEnumDeviceInfo(devices, index, &data)) {
        char *ids = NULL;
        DWORD size = 0;
        DWORD type = 0;

        index++;
        (void)SetupDiGetDeviceRegistryPropertyA(devices, &data,
            SPDRP_HARDWAREID, &type, NULL, 0, &size);
        if (size == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        ids = malloc(size + 2);
        if (ids == NULL) {
            continue;
        }
        if (SetupDiGetDeviceRegistryPropertyA(devices, &data, SPDRP_HARDWAREID,
                &type, (BYTE *)ids, size, NULL) != FALSE
            && (strstr(ids, "VEN_1022&DEV_2000") != NULL)) {
            SP_DEVINFO_DATA parent = data;
            char instance[512] = {0};
            BOOL reboot = FALSE;
            BOOL ok;

            (void)SetupDiGetDeviceInstanceIdA(devices, &parent, instance,
                                              sizeof instance, NULL);
            report_str("nic.instance", instance);
            report_str("nic.hardware_id", ids);
            ok = UpdateDriverForPlugAndPlayDevicesA(
                NULL, instance, "C:\\windows\\inf\\netamd2.inf",
                INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &reboot);
            report("nic.install", ok ? 1u : 0u);
            if (!ok) {
                report("nic.install.err", (unsigned long)GetLastError());
            }
            report("nic.reboot", reboot ? 1u : 0u);
            installed = 1;
            free(ids);
            break;
        }
        free(ids);
    }
    SetupDiDestroyDeviceInfoList(devices);
    report("nic.device_found", installed ? 1u : 0u);
    return installed;
}

static void report_adapters(void)
{
    ULONG size = 0;
    PIP_ADAPTER_INFO info = NULL;
    DWORD rc = GetAdaptersInfo(NULL, &size);

    if (rc != ERROR_BUFFER_OVERFLOW || size == 0) {
        report("adapter.query.err", (unsigned long)rc);
        return;
    }
    info = malloc(size);
    if (info == NULL) {
        return;
    }
    rc = GetAdaptersInfo(info, &size);
    if (rc != ERROR_SUCCESS) {
        report("adapter.query.err", (unsigned long)rc);
        free(info);
        return;
    }
    unsigned long count = 0;
    for (PIP_ADAPTER_INFO walk = info; walk != NULL; walk = walk->Next) {
        char name[96];
        (void)snprintf(name, sizeof name, "adapter.%lu.ip", count);
        report_str(name, walk->IpAddressList.IpAddress.String);
        (void)snprintf(name, sizeof name, "adapter.%lu.mac", count);
        char mac[32];
        (void)snprintf(mac, sizeof mac, "%02x:%02x:%02x:%02x:%02x:%02x",
            walk->Address[0], walk->Address[1], walk->Address[2],
            walk->Address[3], walk->Address[4], walk->Address[5]);
        report_str(name, mac);
        count++;
    }
    report("adapter.count", count);
    free(info);
}

int main(int argc, char **argv)
{
    int expanded;

    if (argc > 1 && argv[1][0] != '\0') {
        record = fopen(argv[1], "w");
    }
    expanded = system("expand -F:pcntpci5.sys \"C:\\windows\\driver cache\\i386\\driver.cab\" C:\\windows\\system32\\drivers >nul");
    report("nic.expand", expanded == 0 ? 1u : 0u);
    install_network_device();
    report_adapters();

    if (record != NULL) {
        fprintf(record, "all NIC install observations complete\n");
        fflush(record);
        (void)_commit(_fileno(record));
        fclose(record);
        Sleep(8000);
        (void)WinExec("shutdown -s -f -t 0", SW_HIDE);
    }
    return 0;
}
