# Booting the source-built `ntoskrnl.exe` on a real XP installation

**Status: verified.** Our kernel, built by the tree's own razzle toolchain, boots a
full Windows XP SP1 installation to a working desktop.

## The result

| Artifact | Value |
|---|---|
| Our kernel | `Source/XPSP1/binaries.x86fre/ntoskrnl.exe`, 1,918,976 bytes, MD5 `96896c94901af22ac60418a6df085bfa` |
| Microsoft's kernel | 2,042,240 bytes, MD5 `b9080d97dbd631aadf9128f7316958d2` |
| Installed at | `xp.vmdk` → `\WINDOWS\system32\ntoskrnl.exe` |
| Boot outcome | Bliss/Luna desktop, Start menu, taskbar clock advancing, `cmd.exe` usable |

The kernel was confirmed in place by reading the file out of the VMDK image
after a clean shutdown and hashing it: the embedded kernel region is byte-for-byte
our build, not Microsoft's.

## Why the swap is hard

`ntoskrnl.exe` is locked by the running kernel. In-guest replacement fails in every
form (Explorer replace dialog accepts "Yes" and does nothing; `copy /y` returns
"The system cannot find the file specified"). Recovery Console can do it, but a
1990s-style VNC keyboard bridge cannot reliably type `:` — VMware's VNC server
drops shift-modified punctuation, and Recovery Console needs `D:\` paths.

## What works: patch the VMDK from the host, no root

`tools/ntfspatch.py` reads and writes files inside a monolithicSparse VMDK by
parsing the VMDK grain directory plus NTFS's MFT, `$INDEX_ROOT`, and
`$INDEX_ALLOCATION`. No loop device, no `nbd`, no `sudo`, no VMware Tools.

```bash
# what is installed?
python3 tools/ntfspatch.py xp.vmdk ls
python3 tools/ntfspatch.py xp.vmdk get '\WINDOWS\system32\ntoskrnl.exe' /tmp/ms.exe

# swap in our kernel (file must be running-free: VM powered off)
python3 tools/ntfspatch.py xp.vmdk put '\WINDOWS\system32\ntoskrnl.exe' \
    Source/XPSP1/binaries.x86fre/ntoskrnl.exe
```

## Two traps that cost real time

**1. Do not change the file's size in its MFT record.** Our kernel is 123,264
bytes smaller than Microsoft's. Editing the `$DATA` real-size field corrupts the
record: `dir` then reports `0 ntoskrnl.exe` and the file becomes unopenable
("No such file or folder"), even though the already-booted kernel keeps running.
**Keep the original size** and zero-pad our kernel to match. Windows loads the PE
image according to the PE headers and ignores trailing padding.

**2. Re-applying the update-sequence array when rewriting an MFT record destroys
it.** Write file *data* through the runlist; leave MFT records alone.

Back up before any swap (`cp xp.vmdk xp-before-kernelswap.vmdk`); restoring the
backup is the recovery path, and it works.

## Booting it

```bash
rm -rf /home/marocos/vms/xpvmware/*.lck
vmrun -T ws start ~/vms/xpvmware/xp.vmx nogui
```

The `.vmx` must have the XP disk as IDE master with no slave present:

```
ide0:0.present = "TRUE"
ide0:0.fileName = "xp.vmdk"
ide0:0.deviceType = "disk"
```

VMware silently rewrites the `.vmx` from memory on shutdown, so edit the file only
while the VM is off and verify with `grep` after each start. A slave device with no
master makes the VM refuse to power on ("IDE slave with no master at 'ide0:1'").

## Recovery Console (the interactive route, if ever needed)

Boot the XP ISO with both disks attached (XP as `ide0:0` master, XP ISO on
`ide1:0`), poll the screen and press a key at "Press any key to boot from CD".
Then `R` → `1` → Enter for a blank Administrator password. `map` shows the letter
assignment. The blocker is that `vncdo` cannot produce `:` — no keysym variant
(`colon`, `shift-colon`, `shift-semicolon`, raw `0x3a`) works against VMware's VNC
server. Use the host-side patcher instead.
