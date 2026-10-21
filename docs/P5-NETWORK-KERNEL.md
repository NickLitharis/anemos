# The kernel network stack (P5 exception)

## Decision

The project plan excluded P5, the kernel proper. On 2026-10-21 the owner
extended that boundary for one component only: the network stack. P4 cannot
replace `tcpip.sys` from user space, and the goal is a real stack, not a
surface. Everything else in P5 stays out of scope.

The licence decision does not change. MIT stays, ReactOS stays rejected, and
lwIP (BSD-3-Clause) is the TCP/IP engine.

## What "real stack" means on XP

Winsock programs do not talk to `tcpip.sys` directly. The path is:

```
application -> ws2_32.dll -> afd.sys -> TDI -> tcpip.sys -> NDIS -> NIC driver
```

`afd.sys` and the NDIS miniport stay Microsoft's. Our driver must:

1. bind to the NIC through NDIS as a protocol driver, so it receives and sends
   Ethernet frames;
2. implement ARP, IPv4, ICMP, TCP and UDP on those frames (lwIP does this);
3. expose the TDI device interface (`\Device\Tcp`, `\Device\Udp`, ...) with the
   IOCTL contract that `afd.sys` expects;
4. load in place of `tcpip.sys`, or alongside it on a test image with the
   service name changed.

TDI is the XP-era interface. WSK arrived with Vista and is not the target.

## Milestones

| Step | Work | Gate |
|---|---|---|
| K1 | Loadable driver skeleton: `DriverEntry`, device, symbolic link, IOCTL ping, unload | ✅ **passed 2026-10-22**: service starts, `\\.\AnemosNet` opens, the ping IOCTL returns `ANEM`, and the service stops; certificate in `anemos/net/kernel/xp/` |
| K2 | lwIP in kernel, `NO_SYS`, kernel clock and pool-backed memory, an internal netif | kernel self-test returns a known checksum |
| K3 | NDIS protocol driver: bind, receive, send frames | guest can ARP and ping a host |
| K4 | TDI surface compatible with `afd.sys` | `ping`, then `telnet`/`ftp`/HTTP through Winsock |
| K5 | Replace `tcpip.sys` on the test image, run P4's Winsock certificate against the new stack | 47/47 or documented differences; DHCP and DNS work |
| K6 | Throughput | not worse than XP's stack on the same VM |

## K1 facts (do not rediscover)

Getting one driver to load took five host-side discoveries. All are verified in
the guest and are now encoded in `anemos/net/kernel/`:

- **The PE checksum is checked at load.** A zero checksum fails with
  `STATUS_IMAGE_CHECKSUM_MISMATCH` (`0xC0000221`), which the SCM reports as the
  generic `ERROR_BAD_DRIVER` (2001). Patch bytes `PE+0x58` after all other
  header edits, then embed the image.
- **A `.data` section with zero raw size was rejected.** The driver loaded only
  after the completion pointer got a non-NULL initializer, which makes the
  section file-backed. The mechanism is not documented; the workaround is.
- Characteristics **0x10E**, SectionAlignment/FileAlignment **0x80**, subsystem
  **5.01**, and a `.reloc` section match XP's own `beep.sys`/`tcpip.sys`.
- `IofCompleteRequest` is **FASTCALL** and does not bind through the generated
  stdcall import library; resolve it with `MmGetSystemRoutineAddress`.
- The deployment path works: a normal user-mode exe writes the `.sys` to
  `System32\drivers`, creates and starts the service, talks to the device, and
  stops it. The harness only swaps that exe over `mspaint.exe`.

## Engineering constraints

- clang 23.1.3 with mingw-w64's DDK headers. No WDK installation.
- Import library for `ntoskrnl.exe` is generated from the XP export list, so
  every import is checked against XP itself.
- Kernel code rules: no C runtime, no floating point, no SEH assumptions,
  bounded IRQL use, pool tags, and explicit unload.
- Test deployment stays host-side: the harness swaps a user-mode exe into
  `mspaint.exe`; that exe writes the `.sys`, installs the service, runs the
  IOCTL test, unloads, and shuts the guest down.
- A failed driver load can bugcheck XP. The VM disk is disposable and the
  pristine image is never touched.

## Risks

- TDI is undocumented in detail and is the largest unknown. The oracle is XP's
  own `tcpip.sys` and the observed IOCTL traffic, not a public spec.
- Kernel debugging is not set up in this VM. A crash costs a boot cycle, not a
  debugger session.
- `afd.sys` may assume details of the TDI provider beyond the device names and
  IOCTL set. K4 will find out.
- lwIP is not thread-safe and has no OS. All entry points must run in one
  serialised context at `PASSIVE_LEVEL`, or be protected explicitly.
