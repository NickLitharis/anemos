/* P3.5 experiment: can anything short of patching advapi32 load our provider
 * through CryptoAPI on this XP build?
 *
 * Registers the provider (Image Path = the DLL the harness installed), then
 * tries CryptAcquireContext with the flag combinations the documentation
 * leaves open, recording the exact error for each. Loads the DLL directly as
 * a control so a failure is attributable to the signature check, not to a
 * broken file. Cleans the registry key up afterwards.
 *
 * usage: sig_probe.exe <provider-dll-path> [result-file]
 */
#include <windows.h>
#include <wincrypt.h>
#include <io.h>
#include <stdio.h>
#include <string.h>

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
    printf("%s %s\n", name, value);
    if (record != NULL) {
        fprintf(record, "%s %s\n", name, value);
    }
}

static unsigned long try_acquire(const char *label, const char *provider,
                                 DWORD flags, int with_container)
{
    HCRYPTPROV prov = 0;
    BOOL ok = CryptAcquireContextA(&prov, with_container ? "container" : NULL,
                                   provider, PROV_RSA_FULL, flags);
    DWORD err = ok ? 0 : GetLastError();
    char name[96];

    (void)snprintf(name, sizeof name, "acquire.%s.rc", label);
    report(name, ok ? 1u : 0u);
    (void)snprintf(name, sizeof name, "acquire.%s.err", label);
    report(name, (unsigned long)err);
    if (ok) {
        CryptReleaseContext(prov, 0);
    }
    return err;
}

int main(int argc, char **argv)
{
    static const char *KEY =
        "Software\\Microsoft\\Cryptography\\Defaults\\Provider\\Anemos Probe";
    const char *dll = (argc > 1) ? argv[1] : NULL;
    HKEY key = NULL;
    DWORD disposition = 0;
    LONG rc;
    HMODULE module;
    DWORD type = PROV_RSA_FULL;
    DWORD exists = 0;
    DWORD size = 0;
    DWORD err;

    if (argc > 2 && argv[2][0] != '\0') {
        record = fopen(argv[2], "w");
    }
    report_str("dll.path", dll == NULL ? "(none)" : dll);

    module = dll == NULL ? NULL : LoadLibraryA(dll);
    report("dll.load", module != NULL ? 1u : 0u);
    if (module == NULL) {
        report("dll.load.err", (unsigned long)GetLastError());
    }

    rc = RegCreateKeyExA(HKEY_LOCAL_MACHINE, KEY, 0, NULL, 0, KEY_WRITE,
                         NULL, &key, &disposition);
    report("registry.create.ok", rc == ERROR_SUCCESS ? 1u : 0u);
    report("registry.create.err", (unsigned long)rc);
    if (rc == ERROR_SUCCESS) {
        rc = RegSetValueExA(key, "Image Path", 0, REG_SZ, (const BYTE *)dll,
                            (DWORD)strlen(dll) + 1);
        report("registry.image_path.ok", rc == ERROR_SUCCESS ? 1u : 0u);
        (void)RegSetValueExA(key, "Type", 0, REG_DWORD, (const BYTE *)&type,
                             (DWORD)sizeof type);
    }

    err = try_acquire("verifycontext", "Anemos Probe", CRYPT_VERIFYCONTEXT, 0);
    (void)err;
    err = try_acquire("verify_silent", "Anemos Probe",
                      CRYPT_VERIFYCONTEXT | CRYPT_SILENT, 0);
    (void)err;
    err = try_acquire("newkeyset", "Anemos Probe", CRYPT_NEWKEYSET, 1);
    (void)err;
    err = try_acquire("plain", "Anemos Probe", 0, 0);
    (void)err;
    err = try_acquire("container", "Anemos Probe", 0, 1);
    (void)err;
    /* Control: the stock provider must still be reachable. */
    (void)try_acquire("default_provider", NULL, CRYPT_VERIFYCONTEXT, 0);

    if (key != NULL) {
        /* Does the provider key carry a Signature value at all? */
        size = 0;
        rc = RegQueryValueExA(key, "Signature", NULL, &exists, NULL, &size);
        report("registry.signature.query", rc == ERROR_SUCCESS ? 1u : 0u);
        report("registry.signature.size", (unsigned long)size);
        RegCloseKey(key);
    }
    (void)RegDeleteKeyA(HKEY_LOCAL_MACHINE, KEY);
    report("registry.delete.ok", 1u);

    if (module != NULL) {
        FreeLibrary(module);
    }
    if (record != NULL) {
        fprintf(record, "all signature observations complete\n");
        fflush(record);
        (void)_commit(_fileno(record));
        fclose(record);
        Sleep(8000);
        /* Power the guest off from inside: the Run-box text fallback can be
         * lost to a focus thief, and a hard cut risks the certificate. */
        (void)WinExec("shutdown -s -f -t 0", SW_HIDE);
    }
    printf("all signature observations complete\n");
    return 0;
}
