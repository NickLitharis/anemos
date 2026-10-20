# BOOT-TEST — XP kernel boot attempt

Approach: A (use ISO machinery) + hand VBR. Did NOT extract canonical XP VBR (no ms-sys, no sudo).
Image: /tmp/xpboot.img (FAT16, MBR at 0, VBR at 7e00, NTLDR not present — missing from image).
QEMU: "Booting from Hard Disk..." then hang at 10s/30s/60s. No NTLDR load; no kernel execution.
Screen text observed (actual): same hang, no bugcheck — kernel never reached.
Blocker: VBR does not locate/load NTLDR (files missing + VBR logic incomplete); registry + NTDETECT also absent, so even if NTLDR loaded it would BSOD.
Result: FAILURE. Kernel built correctly (1464 exports, verified) but boot sector cannot load it.
