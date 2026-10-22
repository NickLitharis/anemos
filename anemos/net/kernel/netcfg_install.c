/* Install AnemosNet as a network protocol through netcfgx (INetCfg).
 *
 * Writing registry keys by hand did not make NDIS bind; netcfg's Install +
 * Apply is what notifies the binding engine. This writes a NetTrans INF, calls
 * INetCfgClassSetup::Install, applies, then checks the driver and binding.
 */
#include <windows.h>
#include <objbase.h>
#include <setupapi.h>
#include <stdio.h>
#include <io.h>
#include <string.h>
#include "driver_blob.h"

#define ANEM_PING_SIGNATURE 0x414E454DUL
#define IOCTL_ANEMOS_PING CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NET_SELFTEST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NDIS_STATE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_ANEMOS_NDIS_REENUM CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DRIVER_PATH "C:\\windows\\system32\\drivers\\anemosnet.sys"
#define INF_PATH "C:\\windows\\inf\\anemosnet.inf"

static const GUID clsid_cnetcfg = {0x5B035261,0x40F9,0x11D1,{0xAA,0xEC,0x00,0x80,0x5F,0xC1,0x27,0x0E}};
static const GUID iid_inetcfg = {0xC0E8AE93,0x306E,0x11D1,{0xAA,0xCF,0x00,0x80,0x5F,0xC1,0x27,0x0E}};
static const GUID iid_inetcfg_lock = {0xC0E8AE9F,0x306E,0x11D1,{0xAA,0xCF,0x00,0x80,0x5F,0xC1,0x27,0x0E}};
static const GUID iid_inetcfg_classsetup = {0xC0E8AE9D,0x306E,0x11D1,{0xAA,0xCF,0x00,0x80,0x5F,0xC1,0x27,0x0E}};
static const GUID guid_nettrans = {0x4D36E975,0xE325,0x11CE,{0xBF,0xC1,0x08,0x00,0x2B,0xE1,0x03,0x18}};

typedef struct INetCfg INetCfg;
typedef struct INetCfgLock INetCfgLock;
typedef struct INetCfgClassSetup INetCfgClassSetup;
typedef struct INetCfgComponent INetCfgComponent;

struct INetCfgVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INetCfg *, const GUID *, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(INetCfg *);
    ULONG (STDMETHODCALLTYPE *Release)(INetCfg *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(INetCfg *, void *);
    HRESULT (STDMETHODCALLTYPE *Uninitialize)(INetCfg *);
    HRESULT (STDMETHODCALLTYPE *Apply)(INetCfg *);
    HRESULT (STDMETHODCALLTYPE *Cancel)(INetCfg *);
    HRESULT (STDMETHODCALLTYPE *EnumComponents)(INetCfg *, const GUID *, void **);
    HRESULT (STDMETHODCALLTYPE *FindComponent)(INetCfg *, const WCHAR *, void **);
    HRESULT (STDMETHODCALLTYPE *QueryNetCfgClass)(INetCfg *, const GUID *, const GUID *, void **);
};
struct INetCfg { struct INetCfgVtbl *lpVtbl; };

struct INetCfgLockVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INetCfgLock *, const GUID *, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(INetCfgLock *);
    ULONG (STDMETHODCALLTYPE *Release)(INetCfgLock *);
    HRESULT (STDMETHODCALLTYPE *AcquireWriteLock)(INetCfgLock *, DWORD, const WCHAR *, WCHAR **);
    HRESULT (STDMETHODCALLTYPE *ReleaseWriteLock)(INetCfgLock *);
};
struct INetCfgLock { struct INetCfgLockVtbl *lpVtbl; };

typedef enum { OBO_USER = 1, OBO_COMPONENT = 2, OBO_SOFTWARE = 3 } OBO_TOKEN_TYPE;
typedef struct {
    OBO_TOKEN_TYPE Type;
    INetCfgComponent *pncc;
    const WCHAR *manufacturer;
    const WCHAR *product;
    const WCHAR *display;
    BOOL registered;
} OBO_TOKEN;

struct INetCfgClassSetupVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INetCfgClassSetup *, const GUID *, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(INetCfgClassSetup *);
    ULONG (STDMETHODCALLTYPE *Release)(INetCfgClassSetup *);
    HRESULT (STDMETHODCALLTYPE *SelectAndInstall)(INetCfgClassSetup *, HWND, OBO_TOKEN *, INetCfgComponent **);
    HRESULT (STDMETHODCALLTYPE *Install)(INetCfgClassSetup *, const WCHAR *, OBO_TOKEN *, DWORD, DWORD, const WCHAR *, const WCHAR *, INetCfgComponent **);
    HRESULT (STDMETHODCALLTYPE *DeInstall)(INetCfgClassSetup *, INetCfgComponent *, OBO_TOKEN *, WCHAR **);
};
struct INetCfgClassSetup { struct INetCfgClassSetupVtbl *lpVtbl; };

struct INetCfgComponentVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(INetCfgComponent *, const GUID *, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(INetCfgComponent *);
    ULONG (STDMETHODCALLTYPE *Release)(INetCfgComponent *);
};
struct INetCfgComponent { struct INetCfgComponentVtbl *lpVtbl; };

static FILE *record;

static void report(const char *name, long value)
{
    printf("%s %ld\n", name, value);
    if (record) fprintf(record, "%s %ld\n", name, value);
}

static int write_file(const char *path, const void *data, DWORD size)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL ok;
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, data, size, &written, NULL);
    CloseHandle(f);
    return ok && written == size;
}

static const char inf_text[] =
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "Class=NetTrans\r\n"
    "ClassGuid={4D36E975-E325-11CE-BFC1-08002BE10318}\r\n"
    "Provider=%P%\r\n"
    "[Manufacturer]\r\n"
    "%M%=Anemos,NTx86\r\n"
    "[Anemos.NTx86]\r\n"
    "%AN%=AnemosNet.ndi,ms_anemosnet\r\n"
    "[AnemosNet.ndi]\r\n"
    "Characteristics=0xA0\r\n"
    "AddReg=AnemosNet.reg\r\n"
    "[AnemosNet.reg]\r\n"
    "HKR,Ndi\\Interfaces,UpperRange,0,\"noupper\"\r\n"
    "HKR,Ndi\\Interfaces,LowerRange,0,\"ndis5\"\r\n"
    "HKR,Ndi,Service,0,\"AnemosNet\"\r\n"
    "[AnemosNet.ndi.Services]\r\n"
    "AddService=AnemosNet,2,AnemosNet.Service\r\n"
    "[AnemosNet.Service]\r\n"
    "ServiceType=1\r\n"
    "StartType=1\r\n"
    "ErrorControl=1\r\n"
    "ServiceBinary=%12%\\anemosnet.sys\r\n"
    "LoadOrderGroup=TDI\r\n"
    "[Strings]\r\n"
    "P=\"Anemos\"\r\n"
    "M=\"Anemos\"\r\n"
    "AN=\"Anemos Network Protocol\"\r\n";

int main(int argc, char **argv)
{
    INetCfg *netcfg = NULL;
    INetCfgLock *lock = NULL;
    INetCfgClassSetup *setup = NULL;
    INetCfgComponent *component = NULL;
    WCHAR *lock_desc = NULL;
    OBO_TOKEN obo;
    HRESULT hr;
    HANDLE device;
    ULONG answer = 0, state[6] = {0};
    DWORD returned = 0;

    if (argc > 1 && argv[1][0]) record = fopen(argv[1], "w");
    report("driver.write", write_file(DRIVER_PATH, driver_blob, (DWORD)sizeof driver_blob));
    report("inf.write", write_file(INF_PATH, inf_text, (DWORD)sizeof inf_text - 1));
    report("inf.copy", SetupCopyOEMInfA(INF_PATH, NULL, SPOST_PATH, 0,
                                        NULL, 0, NULL, NULL) ? 1 : 0);

    hr = CoInitialize(NULL);
    report("com.init", hr);
    hr = CoCreateInstance(&clsid_cnetcfg, NULL, CLSCTX_ALL, &iid_inetcfg, (void **)&netcfg);
    report("netcfg.create", hr);
    if (FAILED(hr)) goto done;
    hr = netcfg->lpVtbl->QueryInterface(netcfg, &iid_inetcfg_lock, (void **)&lock);
    report("netcfg.lock_qi", (long)hr);
    if (SUCCEEDED(hr)) {
        hr = lock->lpVtbl->AcquireWriteLock(lock, 15000, L"AnemosNet", &lock_desc);
        report("netcfg.lock", (long)hr);
        if (lock_desc != NULL) CoTaskMemFree(lock_desc);
    }
    hr = netcfg->lpVtbl->Initialize(netcfg, NULL);
    report("netcfg.init", (long)hr);
    if (FAILED(hr)) goto done;
    hr = netcfg->lpVtbl->QueryNetCfgClass(netcfg, &guid_nettrans,
                                          &iid_inetcfg_classsetup,
                                          (void **)&setup);
    report("netcfg.class", hr);
    memset(&obo, 0, sizeof obo);
    obo.Type = OBO_USER;
    if (SUCCEEDED(hr)) {
        hr = setup->lpVtbl->Install(setup, L"ms_anemosnet", &obo, 0, 0,
                                    NULL, NULL, &component);
        report("netcfg.install", (long)hr);
    }
    hr = netcfg->lpVtbl->Apply(netcfg);
    report("netcfg.apply", (long)hr);
    if (component) component->lpVtbl->Release(component);
    if (setup) setup->lpVtbl->Release(setup);
    if (lock) {
        (void)lock->lpVtbl->ReleaseWriteLock(lock);
        lock->lpVtbl->Release(lock);
    }
    (void)netcfg->lpVtbl->Uninitialize(netcfg);
    netcfg->lpVtbl->Release(netcfg);

done:
    {
        HKEY skey = NULL;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                "SYSTEM\\CurrentControlSet\\Services\\AnemosNet",
                0, KEY_ALL_ACCESS, &skey) == ERROR_SUCCESS) {
            DWORD start = 1;
            (void)RegSetValueExA(skey, "Group", 0, REG_SZ,
                                 (const BYTE *)"TDI", 4);
            (void)RegSetValueExA(skey, "Start", 0, REG_DWORD,
                                 (const BYTE *)&start, sizeof start);
            RegCloseKey(skey);
        }
    }
    {
        SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        SC_HANDLE svc = scm ? OpenServiceA(scm, "AnemosNet", SERVICE_ALL_ACCESS) : NULL;
        report("service.open", svc != NULL);
        if (svc != NULL) {
            report("service.start", StartServiceA(svc, 0, NULL) ? 1 : 0);
            CloseServiceHandle(svc);
        }
        if (scm != NULL) CloseServiceHandle(scm);
    }
    Sleep(1000);
    device = CreateFileA("\\\\.\\AnemosNet", GENERIC_READ | GENERIC_WRITE, 0,
                         NULL, OPEN_EXISTING, 0, NULL);
    report("device.open", device != INVALID_HANDLE_VALUE);
    if (device != INVALID_HANDLE_VALUE) {
        (void)DeviceIoControl(device, IOCTL_ANEMOS_NDIS_REENUM, NULL, 0, NULL, 0, &returned, NULL);
        for (int i = 0; i < 25; i++) {
            if (DeviceIoControl(device, IOCTL_ANEMOS_NDIS_STATE, NULL, 0,
                                state, sizeof state, &returned, NULL) && state[0]) break;
            Sleep(200);
        }
        report("ndis.bound", state[0]);
        report("ndis.calls", state[3]);
        report("ndis.open_status", state[4]);
        (void)DeviceIoControl(device, IOCTL_ANEMOS_PING, NULL, 0, &answer,
                              sizeof answer, &returned, NULL);
        report("ioctl.ping", answer == ANEM_PING_SIGNATURE);
        CloseHandle(device);
    }
    if (record) {
        fprintf(record, "all netcfg observations complete\n");
        fflush(record);
        (void)_commit(_fileno(record));
        fclose(record);
        Sleep(8000);
        (void)WinExec("shutdown -s -f -t 0", SW_HIDE);
    }
    return 0;
}
