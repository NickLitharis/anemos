/* K1 test: install the Anemos driver, talk to it, unload it.
 *
 * The driver image is embedded in this executable, so the harness only has to
 * deliver one file. The test writes the .sys, creates and starts a kernel
 * service, opens \\.\AnemosNet, sends the ping IOCTL, stops and deletes the
 * service, then writes a certificate and shuts the guest down.
 */
#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <wchar.h>

#include "driver_blob.h"

#define ANEM_PING_SIGNATURE 0x414E454DUL   /* 'ANEM' */
#define IOCTL_ANEMOS_PING \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DRIVER_PATH "C:\\windows\\system32\\drivers\\anemosnet.sys"
#define DRIVER_REG_PATH L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\AnemosNet"

typedef struct {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} ANEM_UNICODE_STRING;
typedef LONG (NTAPI *PFN_NTLOADDRIVER)(ANEM_UNICODE_STRING *);
typedef LONG (NTAPI *PFN_NTUNLOADDRIVER)(ANEM_UNICODE_STRING *);
static PFN_NTLOADDRIVER nt_load;
static PFN_NTUNLOADDRIVER nt_unload;
static int nt_loaded;

static LONG nt_call(PFN_NTLOADDRIVER fn, const WCHAR *path)
{
    ANEM_UNICODE_STRING name;

    name.Buffer = (PWSTR)path;
    name.Length = (USHORT)(wcslen(path) * sizeof(WCHAR));
    name.MaximumLength = (USHORT)(name.Length + sizeof(WCHAR));
    return fn(&name);
}

static FILE *record;

static void report(const char *name, unsigned long value)
{
    printf("%s %lu\n", name, value);
    if (record != NULL) {
        fprintf(record, "%s %lu\n", name, value);
    }
}

static int write_driver(void)
{
    HANDLE file = CreateFileA(DRIVER_PATH, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL ok;

    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ok = WriteFile(file, driver_blob, (DWORD)sizeof driver_blob, &written, NULL);
    CloseHandle(file);
    return ok && written == sizeof driver_blob ? 1 : 0;
}

int main(int argc, char **argv)
{
    SC_HANDLE manager = NULL;
    SC_HANDLE service = NULL;
    HANDLE device = INVALID_HANDLE_VALUE;
    ULONG answer = 0;
    DWORD returned = 0;
    BOOL ok;

    if (argc > 1 && argv[1][0] != '\0') {
        record = fopen(argv[1], "w");
    }

    report("driver.write", write_driver());
    manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    report("service.manager", manager != NULL ? 1u : 0u);
    if (manager != NULL) {
        /* Control: XP's own beep.sys through the same SCM path. If this fails
         * too, the harness is wrong, not the image. */
        SC_HANDLE ctl = NULL;
        (void)CopyFileA("C:\\windows\\system32\\drivers\\beep.sys",
                        "C:\\windows\\system32\\drivers\\anemosctl.sys", FALSE);
        ctl = CreateServiceA(manager, "AnemosCtl", "AnemosCtl",
                             SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                             SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                             "C:\\windows\\system32\\drivers\\anemosctl.sys",
                             NULL, NULL, NULL, NULL, NULL);
        if (ctl == NULL) {
            report("control.beep.create.err", (unsigned long)GetLastError());
        } else {
            BOOL ctl_ok = StartServiceA(ctl, 0, NULL);
            report("control.beep.start", ctl_ok ? 1u : 0u);
            if (!ctl_ok) {
                report("control.beep.start.err", (unsigned long)GetLastError());
            } else {
                SERVICE_STATUS cs;
                (void)ControlService(ctl, SERVICE_CONTROL_STOP, &cs);
            }
            DeleteService(ctl);
            CloseServiceHandle(ctl);
            (void)DeleteFileA("C:\\windows\\system32\\drivers\\anemosctl.sys");
        }
        service = CreateServiceA(manager, "AnemosNet", "AnemosNet",
                                 SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                 DRIVER_PATH, NULL, NULL, NULL, NULL, NULL);
        report("service.create", service != NULL ? 1u : 0u);
        if (service == NULL) {
            report("service.create.err", (unsigned long)GetLastError());
        }
    }
    if (service != NULL) {
        ok = StartServiceA(service, 0, NULL);
        report("service.start", ok ? 1u : 0u);
        if (!ok) {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");

            report("service.start.err", (unsigned long)GetLastError());
            if (ntdll != NULL) {
                nt_load = (PFN_NTLOADDRIVER)(void *)GetProcAddress(ntdll, "NtLoadDriver");
                nt_unload = (PFN_NTUNLOADDRIVER)(void *)GetProcAddress(ntdll, "NtUnloadDriver");
            }
            if (nt_load != NULL) {
                LONG st = nt_call(nt_load, DRIVER_REG_PATH);
                report("ntloaddriver.status", (unsigned long)st);
                nt_loaded = st == 0;
            }
        }
    }
    device = CreateFileA("\\\\.\\AnemosNet", GENERIC_READ | GENERIC_WRITE,
                         0, NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) {
        report("device.open.link.err", (unsigned long)GetLastError());
        device = CreateFileA("\\\\.\\GLOBALROOT\\Device\\AnemosNet",
                             GENERIC_READ | GENERIC_WRITE, 0, NULL,
                             OPEN_EXISTING, 0, NULL);
        report("device.open.globalroot", device != INVALID_HANDLE_VALUE ? 1u : 0u);
    }
    report("device.open", device != INVALID_HANDLE_VALUE ? 1u : 0u);
    if (device == INVALID_HANDLE_VALUE) {
        report("device.open.err", (unsigned long)GetLastError());
    } else {
        ok = DeviceIoControl(device, IOCTL_ANEMOS_PING, NULL, 0,
                             &answer, sizeof answer, &returned, NULL);
        report("ioctl.ping", ok ? 1u : 0u);
        if (!ok) {
            report("ioctl.ping.err", (unsigned long)GetLastError());
        }
        report("ioctl.signature", (unsigned long)answer);
        CloseHandle(device);
    }
    if (service != NULL) {
        SERVICE_STATUS status;

        ok = ControlService(service, SERVICE_CONTROL_STOP, &status);
        report("service.stop", ok ? 1u : 0u);
        if (nt_loaded && nt_unload != NULL) {
            report("ntunloaddriver.status",
                   (unsigned long)nt_call((PFN_NTLOADDRIVER)(void *)nt_unload,
                                          DRIVER_REG_PATH));
        }
        report("service.delete", DeleteService(service) ? 1u : 0u);
        CloseServiceHandle(service);
    }
    if (manager != NULL) {
        CloseServiceHandle(manager);
    }
    (void)DeleteFileA(DRIVER_PATH);

    if (record != NULL) {
        fprintf(record, "all kernel M1 observations complete\n");
        fflush(record);
        (void)_commit(_fileno(record));
        fclose(record);
        Sleep(8000);
        (void)WinExec("shutdown -s -f -t 0", SW_HIDE);
    }
    printf("all kernel M1 observations complete\n");
    return 0;
}
