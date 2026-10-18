#!/usr/bin/env python3
"""G2 — the conformance runner.

Boots an XP disk image in VMware, waits for the desktop, drives a fixed probe
sequence through the guest's keyboard (over VNC), captures observations, shuts
the guest down, and extracts the files the guest wrote — host-side, through
tools/ntfspatch.py.

It does this by retargeting the tree's own `~/vms/xpvmware/xp.vmx` at one disk
or the other, while the VM is off, and restoring it afterwards: the control run
and the subject run must present the guest with the same machine (same BIOS
UUID, same MAC, same devices), so the kernel on the disk is the only variable.

The gate: pointed at xp-pristine.vmdk (Microsoft's kernel) and xp.vmdk (ours),
running the same probe, it must report **identical**. If it reports a
difference, the runner is wrong, not the kernels — unless the difference
survives the stability check below.

    runner.py run      --disk <vmdk> --tag <name>    boot, probe, collect
    runner.py compare  <dirA> <dirB>                 diff two collections

Every observation lands in harness/conformance/out/<tag>/:
    meta.json          boot outcome, artifact hashes
    screen-*.bmp       the guest screen after the probe
    guest-*.txt        files written by the guest, pulled from the NTFS image

Stability is checked by running the SAME disk twice (`--tag a1 --tag a2`) and
requiring compare a1 a2 to be identical. A runner that cannot reproduce itself
cannot judge anything.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT_ROOT = Path(__file__).resolve().parent / "out"
NFTPATCH = ROOT / "tools" / "ntfspatch.py"

# One VM, two disks. The control and the subject must present the guest with
# the *same* machine — same BIOS UUID, same MAC, same devices — so the only
# variable between a control run and a subject run is the kernel on the disk.
# So the runner does not create a second VMX: it retargets the tree's own
# xp.vmx (disk swapped while powered off, always restored afterwards).
VMS_DIR = Path.home() / "vms" / "xpvmware"
VMX = VMS_DIR / "xp.vmx"
DEFAULT_DISK = "xp.vmdk"          # the subject, i.e. the disk we patch

VNCDO = Path.home() / ".local" / "bin" / "vncdo"
VNC = "127.0.0.1::5901"          # vncdo wants ::PORT; :PORT means display number
VNC_PASSWORD = "xpvm"
VNC_TIMEOUT = 600                # seconds to wait for the guest desktop

# Commands the probe runs inside the guest, one line each.
#
# Every character here has been measured against this VMware build over VNC:
# letters, digits, space, '.', '-', '/' and '\' survive vncdotool's `type`
# untouched, but its keysym for '>' arrives in the guest as '.' (`type "0>1"`
# prints `0.1`) — so '>' is never sent inside `type`; line_tokens() splits it
# out and sends it as `key shift-.`, which does produce one. ':' and '%' are
# not used anywhere: ':' is the key VMware is documented to drop and '%' was
# never needed, so neither is risked. No drive letters, no variable expansion.
PROBE_COMMANDS = [
    "cd \\",
    "md conftest 2>nul",    # 2>nul: a leftover \conftest from an earlier run would
                            # print an error line and break the screen comparison
    "cd conftest",
    "ver > ver.txt",
    "set > env.txt",
    "assoc > assoc.txt",
    "ftype > ftype.txt",
    "dir /-c /b \\windows\\system32 > sys32.txt",
    "driverquery > drivers.txt",
]
PROBE_GUEST_FILES = ["ver.txt", "env.txt", "assoc.txt", "ftype.txt",
                     "sys32.txt", "drivers.txt"]


def log(msg: str) -> None:
    print(f"[runner] {msg}", flush=True)


def run(cmd: list[str], timeout: int = 120, check: bool = False) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, check=check)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(cmd, 124, "", f"timeout after {timeout}s")


# --------------------------------------------------------------------------
# guest-side plumbing
# --------------------------------------------------------------------------

def vncdo(*tokens: str, timeout: int = 120) -> subprocess.CompletedProcess:
    """Send one vncdotool command. `tokens` are exactly the argv entries
    vncdotool should see for that command, so `type` gets its whole string as a
    single token (spaces intact) while `move` gets x and y separately. Getting
    this wrong types the wrong thing into the guest, so the call sites spell
    it out. Shapes checked against vncdotool's own parser, not assumed."""
    cmd = [str(VNCDO), "-s", VNC, "-p", VNC_PASSWORD, "--delay", "30",
           "--localcursor"]
    cmd.extend(tokens)
    return run(cmd, timeout=timeout)


def line_tokens(line: str) -> list[list[str]]:
    """Split one probe line into vncdotool commands.

    '>' must not travel inside `type` — VMware maps that keysym to '.' (measured:
    `type "0>1"` printed `0.1`), while `key shift-.` produces a real one. Every
    other character in PROBE_COMMANDS was measured going through `type` intact.
    """
    tokens: list[list[str]] = []
    buf = ""
    for ch in line:
        if ch == ">":
            if buf:
                tokens.append(["type", buf])
                buf = ""
            tokens.append(["key", "shift-."])
        else:
            buf += ch
    if buf:
        tokens.append(["type", buf])
    return tokens


def send(tokens: list[str]) -> bool:
    """One vncdotool command; False means the channel broke."""
    r = vncdo(tokens[0], *tokens[1:])
    if r.returncode != 0:
        tail = (r.stderr or "").strip().splitlines()[-1:] or ["?"]
        log(f"  keystroke failed {tokens!r}: {tail[0][:160]}")
        return False
    return True


def wait_for_connection(timeout: int = 60) -> bool:
    """VMware's VNC server only listens once the VM is powered on."""
    probe = OUT_ROOT / ".vnc-probe.bmp"
    for _ in range(max(timeout // 3, 1)):
        if capture(probe):
            return True
        time.sleep(3)
    return False


def capture(path: Path, *, delay: float = 0.0) -> bool:
    if delay:
        time.sleep(delay)
    r = vncdo("capture", str(path))
    return r.returncode == 0 and path.exists() and path.stat().st_size > 1000


def read_bmp(path: Path) -> tuple[int, int, bytes]:
    """Width, height and 24-bit top-down pixels of a Windows BMP."""
    data = path.read_bytes()
    w = int.from_bytes(data[18:22], "little", signed=True)
    h = int.from_bytes(data[22:26], "little", signed=True)
    off = int.from_bytes(data[10:14], "little")
    bottom_up = h > 0
    h = abs(h)
    stride = ((w * 3) + 3) & ~3
    rows = []
    for y in range(h):
        src = off + ((h - 1 - y) if bottom_up else y) * stride
        rows.append(data[src:src + w * 3])
    return w, h, b"".join(rows)


def strip_mean(pixels: bytes, w: int, h: int, rect: tuple[int, int, int, int]) -> float:
    x0, y0, x1, y1 = rect
    total = 0
    n = 0
    for y in range(y0, min(y1, h)):
        row = pixels[y * w * 3:(y + 1) * w * 3]
        for x in range(x0, min(x1, w)):
            b, g, r = row[x * 3:x * 3 + 3]
            total += (b + g + r) / 3
            n += 1
    return total / max(n, 1)


def red_bbox(path: Path) -> tuple[tuple[int, int, int, int] | None, float]:
    """Bounding box and area fraction of 'cmd red' pixels in the frame.

    `color 4f` paints the console window's background red and nothing else on a
    clean XP desktop is red, so this is how the runner proves that cmd.exe owns
    the keyboard — the one thing every later keystroke depends on — and also how
    it learns where the console window is, which is the region it later compares
    pixel-for-pixel between runs (the taskbar clock and notification balloons
    live outside it, and those are not reproducible)."""
    try:
        w, h, px = read_bmp(path)
    except Exception:
        return None, 0.0
    x0, y0, x1, y1 = w, h, -1, -1
    red = 0
    for y in range(h):
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(w):
            b, g, r = row[x * 3:x * 3 + 3]
            # XP's `color 4f` background is (128,0,0), measured — not a bright red
            if r > 100 and g < 70 and b < 70:
                red += 1
                x0, y0 = min(x0, x), min(y0, y)
                x1, y1 = max(x1, x), max(y1, y)
    if x1 < 0:
        return None, 0.0
    return (x0, y0, x1 + 1, y1 + 1), red / float(w * h)


def wait_for_desktop(workdir: Path, timeout: int = VNC_TIMEOUT) -> tuple[bool, float]:
    """The XP taskbar is a bright strip across the bottom; the boot splash and
    the black screen before it are not. Require two consecutive bright frames
    so a single repaint cannot be mistaken for a desktop."""
    deadline = time.monotonic() + timeout
    started = time.monotonic()
    last_bright = False
    while time.monotonic() < deadline:
        bmp = workdir / "screen-boot.bmp"
        if capture(bmp, delay=4.0):
            try:
                w, h, px = read_bmp(bmp)
            except Exception:
                w = h = 0
                px = b""
            if w:
                # taskbar lives in the bottom 30 rows, left 120 px (Start button)
                bright = strip_mean(px, w, h, (0, h - 30, 120, h)) > 30.0
                if bright and last_bright:
                    return True, time.monotonic() - started
                last_bright = bright
        log(f"  waiting for desktop ({int(time.monotonic() - started)}s)")
    return False, timeout


def open_cmd(workdir: Path, attempt: int) -> bool:
    """Get cmd.exe to own the keyboard — and prove it, in pixels.

    Opening cmd is the fragile part and every route but one was measured
    failing: Ctrl+Esc opens the Start menu, but typed characters there drive
    XP's incremental icon selection instead of the Run box (`type cmd` opened
    Control Panel and then the Mouse Properties dialog — seen on screen), and
    dragging to the Start button doesn't work either, because VMware drops bare
    pointer moves and a drag that started on the desktop never activates the
    button. Win+R (`key super-r`) opens the Run box directly, its edit control
    takes `type` reliably, and its OK is disabled while the field is empty, so
    an early Enter is a no-op.

    Anything can hold focus at this point — a boot balloon, the Found New
    Hardware Wizard — and a keystroke typed into the wrong window is a probe
    that silently observed nothing. So after opening we type `color 4f`: if cmd
    really has focus its window turns red, and red pixels are unambiguous. No
    red after the attempts means no probe, reported as a failure.
    """
    seq: list[tuple[list[str], float]] = [
        (["key", "esc"], 1.0), (["key", "esc"], 1.0), (["key", "esc"], 1.0),
        (["key", "super-r"], 2.0),                      # Win+R -> Run box
        (["type", "cmd"], 1.5),                         # -> its edit control
        (["key", "enter"], 6.0),                        # -> cmd.exe
        (["type", "color 4f"], 1.5),                    # paint the proof
        (["key", "enter"], 3.0),
    ]
    for tokens, pause in seq:
        if not send(tokens):
            return False
        time.sleep(pause)

    shot = workdir / f"screen-focus-{attempt}.bmp"
    if not capture(shot):
        return False
    box, frac = red_bbox(shot)
    log(f"  cmd focus attempt {attempt}: red fraction {frac:.3f} box {box}")
    return frac > 0.10


def run_probe(workdir: Path, extra: list[str] | None = None,
              settle: int = 10) -> bool:
    for attempt in range(1, 4):
        if open_cmd(workdir, attempt):
            break
    else:
        log("  cmd.exe never took focus; probe not run")
        return False

    for line in PROBE_COMMANDS + (extra or []):
        for tokens in line_tokens(line):
            if not send(tokens):
                return False
            time.sleep(0.3)
        if not send(["key", "enter"]):
            return False
        time.sleep(3.0 if "driverquery" in line else 2.0)

    time.sleep(settle)      # let the guest finish writing before we cut power
    return True


# --------------------------------------------------------------------------
# host-side plumbing
# --------------------------------------------------------------------------

def running_vms() -> list[Path]:
    r = run(["vmrun", "-T", "ws", "list"], timeout=60)
    return [Path(line.strip()) for line in r.stdout.splitlines()[1:]
            if line.strip().endswith(".vmx")]


def vm_is_running(vmx: Path) -> bool:
    return any(p.resolve() == vmx.resolve() for p in running_vms())


def stop_vm(vmx: Path, mode: str = "hard") -> None:
    if vm_is_running(vmx):
        run(["vmrun", "-T", "ws", "stop", str(vmx), mode], timeout=120)
    for lck in list(vmx.parent.glob("*.lck")) + list(vmx.parent.glob("*.lck/*")):
        shutil.rmtree(lck, ignore_errors=True)


def require_exclusive() -> str | None:
    """A conformance run needs the machine to itself: two guests on one 15 GB
    host boot so slowly that slow becomes indistinguishable from broken."""
    for p in running_vms():
        ours = str(p).startswith(str(VMS_DIR)) or str(p).startswith(str(OUT_ROOT))
        if not ours:
            return f"another VM is running that is not ours: {p}"
    for p in running_vms():
        log(f"stopping leftover VM {p}")
        stop_vm(p, "hard")
    return None


def set_disk(disk_name: str) -> None:
    """Retarget the tree's own VMX at a disk. Only legal while the VM is off,
    and always verified by reading the file back."""
    text = VMX.read_text(encoding="latin-1")
    text = re.sub(r'ide0:0\.fileName = "[^"]*"', f'ide0:0.fileName = "{disk_name}"', text)
    VMX.write_text(text, encoding="latin-1")
    got = re.search(r'ide0:0\.fileName = "([^"]*)"', VMX.read_text(encoding="latin-1"))
    if not got or got.group(1) != disk_name:
        raise RuntimeError(f"failed to retarget {VMX} at {disk_name}")


def start_vm() -> bool:
    r = run(["vmrun", "-T", "ws", "start", str(VMX), "nogui"], timeout=180)
    if r.returncode != 0:
        log(f"vmrun start failed: {(r.stdout + r.stderr).strip()[:300]}")
    return r.returncode == 0


def shutdown_vm(grace: int = 120) -> None:
    """Wait for the guest to power itself off, then push ACPI, then cut power.

    The probe's last act is `shutdown -s`, and only a real guest shutdown
    flushes the write cache: the host's ACPI soft-stop was measured not to
    power this guest off at all (the grace period expired on every run), and a
    hard cut before the flush threw away every file the probe had written —
    the directory existed on disk, its contents did not.
    """
    deadline = time.monotonic() + grace
    while time.monotonic() < deadline:
        if not vm_is_running(VMX):
            log("  guest powered off cleanly")
            return
        time.sleep(3)
    log("  guest did not power itself off; ACPI stop")
    run(["vmrun", "-T", "ws", "stop", str(VMX), "soft"], timeout=60)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if not vm_is_running(VMX):
            log("  guest powered off cleanly")
            return
        time.sleep(3)
    log("  grace period over; hard power cut")
    stop_vm(VMX, "hard")


def extract_guest_files(disk: Path, workdir: Path,
                        names: list[str] | None = None) -> dict[str, str]:
    """Pull the probe's output out of the NTFS image, host-side."""
    hashes: dict[str, str] = {}
    for name in (names if names is not None else PROBE_GUEST_FILES):
        out = workdir / f"guest-{name}"
        r = run(["python3", str(NFTPATCH), str(disk), "get",
                 f"\\conftest\\{name}", str(out)], timeout=180)
        if r.returncode == 0 and out.exists():
            data = out.read_bytes()
            # the guest writes through the FS; trailing slack is allocation, not content
            data = data.rstrip(b"\x00")
            out.write_bytes(data)
            hashes[name] = hashlib.sha256(data).hexdigest()
        else:
            hashes[name] = "MISSING"
            log(f"  could not extract \\conftest\\{name}: {r.stdout.strip()[:120]}"
                f"{r.stderr.strip()[:120]}")
    return hashes


def write_meta(workdir: Path, meta: dict) -> None:
    (workdir / "meta.json").write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n")


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------

def cmd_run(args: argparse.Namespace) -> int:
    disk = Path(args.disk).expanduser().resolve()
    if not disk.exists():
        log(f"no such disk: {disk}")
        return 2
    if disk.parent != VMS_DIR:
        log(f"refusing: {disk} is not next to {VMX}")
        return 2

    workdir = OUT_ROOT / args.tag
    if workdir.exists() and any(workdir.iterdir()):
        # a tag that already has artifacts would mix two runs into one comparison
        log(f"output dir {workdir} is not empty; use a fresh tag")
        return 2
    workdir.mkdir(parents=True, exist_ok=True)
    log(f"tag={args.tag} disk={disk.name}")

    busy = require_exclusive()
    if busy:
        log(busy)
        return 2

    try:
        stop_vm(VMX, "hard")
        set_disk(disk.name)
        if not start_vm():
            return 2

        reached, seconds = wait_for_desktop(workdir, timeout=args.timeout)
        log(f"desktop reached: {reached} ({seconds:.0f}s)")
        if reached:
            time.sleep(10)       # let the desktop finish painting and settle

        probe_ok = False
        if reached and not args.no_probe:
            log("running probe")
            probe_ok = run_probe(workdir, args.probe_cmd, args.settle)

        capture(workdir / "screen-final.bmp", delay=1.0)
        final_box, final_frac = red_bbox(workdir / "screen-final.bmp")

        if probe_ok:
            # A `dir` of \conftest, captured but never compared (its listing
            # carries timestamps): the run keeps the guest's own record of what
            # it produced, so "the files exist" can be settled from a picture.
            send(["type", "dir"])
            time.sleep(0.3)
            send(["key", "enter"])
            time.sleep(2.0)
            capture(workdir / "screen-dir.bmp", delay=0.5)
            # Flush the write cache the only way that works here — the guest
            # shutting itself down (see shutdown_vm).
            send(["type", "shutdown -s"])
            time.sleep(0.3)
            send(["key", "enter"])

        log("shutting down")
        shutdown_vm(grace=args.shutdown_grace)

        collect = PROBE_GUEST_FILES + (args.collect or [])
        hashes = extract_guest_files(disk, workdir, collect) if probe_ok else {}
    finally:
        stop_vm(VMX, "hard")
        set_disk(DEFAULT_DISK)      # leave the tree's VM exactly as we found it

    write_meta(workdir, {
        "disk": str(disk),
        "disk_sha256": hashlib.sha256(disk.read_bytes()[:64 * 1024 * 1024]).hexdigest(),
        "tag": args.tag,
        "boot_reached_desktop": reached,
        "boot_seconds_to_desktop": round(seconds, 1),
        "probe_ran": probe_ok,
        "guest_files": hashes,
        "screen_rect": final_box,
        "screen_red_fraction": round(final_frac, 4),
        "probe_commands": PROBE_COMMANDS + (args.probe_cmd or []),
    })
    log(f"observations in {workdir}")
    return 0 if reached else 1


def cmd_probe(args: argparse.Namespace) -> int:
    """Drive the probe against a VM the caller has already booted."""
    if not wait_for_connection():
        log("no VNC server on :5901")
        return 2
    workdir = OUT_ROOT / args.tag
    workdir.mkdir(parents=True, exist_ok=True)
    return 0 if run_probe(workdir) else 1


def screen_region_hash(bmp: Path, rect: tuple[int, ...]) -> str | None:
    try:
        w, h, px = read_bmp(bmp)
    except Exception:
        return None
    x0, y0, x1, y1 = rect
    rows = b"".join(px[y * w * 3 + x0 * 3:y * w * 3 + x1 * 3]
                    for y in range(y0, min(y1, h)))
    return hashlib.sha256(rows).hexdigest()


def cmd_compare(args: argparse.Namespace) -> int:
    a, b = Path(args.a), Path(args.b)
    ma = json.loads((a / "meta.json").read_text())
    mb = json.loads((b / "meta.json").read_text())
    diffs: list[str] = []

    for key in ("boot_reached_desktop", "probe_ran"):
        if ma.get(key) != mb.get(key):
            diffs.append(f"{key}: {ma.get(key)} != {mb.get(key)}")

    files_a, files_b = ma.get("guest_files", {}), mb.get("guest_files", {})
    for name in sorted(set(files_a) | set(files_b) | set(PROBE_GUEST_FILES)):
        va, vb = files_a.get(name), files_b.get(name)
        if va != vb:
            diffs.append(f"guest file {name}: {va} != {vb}")
        elif va not in (None, "MISSING"):
            fa, fb = a / f"guest-{name}", b / f"guest-{name}"
            if fa.read_bytes() != fb.read_bytes():
                diffs.append(f"guest file {name}: content differs")

    # The console window's position is derived from the pixels themselves
    # (red_bbox), so the region compared is exactly the region the probe owned:
    # the taskbar clock and notification balloons live outside it.
    rect_a, rect_b = ma.get("screen_rect"), mb.get("screen_rect")
    if rect_a != rect_b:
        diffs.append(f"console window rect: {rect_a} != {rect_b}")
    elif rect_a:
        ha = screen_region_hash(a / "screen-final.bmp", tuple(rect_a))
        hb = screen_region_hash(b / "screen-final.bmp", tuple(rect_b))
        if ha != hb:
            diffs.append(f"console window pixels: {ha} != {hb}")

    if diffs:
        print(f"DIFFERENT ({len(diffs)}):")
        for d in diffs:
            print(f"  {d}")
        return 1
    print("IDENTICAL")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_run = sub.add_parser("run", help="boot a disk, probe it, collect observations")
    p_run.add_argument("--disk", required=True)
    p_run.add_argument("--tag", required=True)
    p_run.add_argument("--timeout", type=int, default=VNC_TIMEOUT)
    p_run.add_argument("--no-probe", action="store_true",
                       help="boot only: observe whether the desktop is reached")
    # The three flags below default to exactly the G2 behaviour, so every
    # reproduction command in docs/G2-CONFORMANCE-RUNNER.md stays valid.
    p_run.add_argument("--probe-cmd", action="append", default=None,
                       metavar="LINE",
                       help="extra guest command line, typed after the standard "
                            "probe (repeatable)")
    p_run.add_argument("--settle", type=int, default=10,
                       help="seconds to wait after the last command before "
                            "shutdown (default 10)")
    p_run.add_argument("--collect", action="append", default=None,
                       metavar="NAME",
                       help="extra file to extract from \\conftest (repeatable)")
    p_run.add_argument("--shutdown-grace", type=int, default=120,
                       help="seconds to wait for the guest to power itself off "
                            "before ACPI/hard stop (default 120)")
    p_run.set_defaults(func=cmd_run)

    p_cmp = sub.add_parser("compare", help="diff two observation collections")
    p_cmp.add_argument("a")
    p_cmp.add_argument("b")
    p_cmp.set_defaults(func=cmd_compare)

    p_probe = sub.add_parser("probe", help="drive the probe at an already-booted VM")
    p_probe.add_argument("--tag", required=True)
    p_probe.set_defaults(func=cmd_probe)

    args = ap.parse_args()
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
