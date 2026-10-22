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
| K2 | lwIP in kernel, `NO_SYS`, kernel clock and an internal netif | ✅ **passed 2026-10-22**: the driver loads, a UDP datagram crosses lwIP's loopback netif in kernel context, and the returned FNV-1a checksum equals an independent user-mode implementation (`254371596`). Certificate: `anemos/net/kernel/xp/anemos-k2.txt` |
| K3 | NDIS protocol driver: bind, receive, send frames | ◐ **binding passed 2026-10-22**: netcfg installs the protocol as a NetTrans component, NDIS calls `ProtocolBindAdapter`, and `NdisOpenAdapter` succeeds (`ndis.bound 1`, `bind_successes 1`). Frame receive/send is the next slice. |
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
- Characteristics **0x10E**, subsystem **5.01**, and a `.reloc` section match
  XP's own `beep.sys`/`tcpip.sys`.
- **SectionAlignment 0x80 is only safe while the image is small.** The K1
  driver (about 20 KB) loaded at 0x80. The K2 image (about 108 KB) was rejected
  with no event-log entry at 0x80, and loaded unchanged at SectionAlignment
  **0x1000** / FileAlignment **0x200**. The build now uses 0x1000/0x200.
- `IofCompleteRequest` is **FASTCALL** and does not bind through the generated
  stdcall import library; resolve it with `MmGetSystemRoutineAddress`.
- The deployment path works: a normal user-mode exe writes the `.sys` to
  `System32\drivers`, creates and starts the service, talks to the device, and
  stops it. The harness only swaps that exe over `mspaint.exe`.

## K2 facts

- lwIP is compiled freestanding for the driver (`build-lwip-kernel.sh`). It
  needs local `memcpy`, `memset`, `memcmp`, `memmove`, `strlen`, `strncmp`,
  `atoi`, and the ctype helpers; mingw declares the ctype family as dllimport,
  so the port routes them to local functions from `arch/cc.h`.
- `sys_now` reads `KeQueryInterruptTime` in 32-bit arithmetic (shift then
  divide) so the driver does not need `__udivdi3`.
- The self-test runs entirely in kernel context: `lwip_init`, the built-in
  loopback netif, a UDP PCB, `netif_poll`, and an FNV-1a checksum of the
  received bytes. The user-mode test computes the same checksum independently.

## K3 facts (so far)

- The VM's NIC is now `vlance` (AMD PCnet32). XP's driver cab already had
  `pcntpci5.sys`, and `Windows\inf\netamd2.inf` matches `PCI\VEN_1022&DEV_2000`.
  There is no e1000 INF for VMware's 82545, only the 82543 (`DEV_1004`).
- The guest installed the driver from its own cab and now has adapter
  `192.168.103.129` with MAC `00:0c:29:e0:23:b7`. tcpip and Winsock use it.
- Our driver registers an NDIS 5.1 protocol successfully, but NDIS never calls
  `ProtocolBindAdapter`. Registering after boot does not join the binding
  engine.
- The protocol's `Ndi\Interfaces` keys were added (`UpperRange=noupper`,
  `LowerRange=ndis5`, matching the adapter's `UpperRange=ndis5`), and
  `Linkage\Bind` was set to `\Device\{NetCfgInstanceId}` before the driver
  started. The NIC was then restarted through `SetupDi` property change.
  NDIS still did not call the bind handler in the same boot.
- Opening the adapter directly by `\Device\{NetCfgInstanceId}` returns
  `0xC0010006` (`NDIS_STATUS_OPEN_FAILED`).

The system-start test settled it. The service was installed as
`SERVICE_SYSTEM_START` in group `NDIS`, the guest rebooted, and the driver
loaded at boot (`driver.write=0`, `device.open=1`). NDIS still did not call
`ProtocolBindAdapter`, and direct open still returned `0xC0010006`.

A NetTrans component was then created by hand (`Control\Network\{4D36E975-...}`
with `ComponentId`, `Description`, `InfPath`, `Ndi\Service` and the interface
ranges), and the existing `ms_tcpip` component was temporarily repointed at
our service to borrow its binding identity. NDIS still did not call
`ProtocolBindAdapter`; direct open still returned `0xC0010006`.

Conclusion: manual registry state is not enough. NDIS binding is created by
the network configuration machinery (`netcfgx`), which installs a component
through `INetCfg` and notifies NDIS. Writing the keys that installation
produces does not run that notification. The next step is to install through
`INetCfg`/`netcfgx` (or a proper NetTrans INF handled by it), not to add more
keys.

The guest was restored afterwards: the borrowed component value was reverted,
the AnemosNet component keys were removed, its service and driver file were
deleted, and `ipconfig` in the guest still shows 192.168.103.129 with tcpip
working.

### netcfgx attempt

A real NetTrans INF plus a manual `INetCfg` client reached netcfgx:
`CoCreateInstance(CLSID_CNetCfg)` and `Initialize` succeed, and
`QueryNetCfgClass(NetTrans, IID_INetCfgClassSetup)` succeeds. The write lock is
the current blocker: `QueryInterface(IID_INetCfgLock)` returns S_OK, but
`AcquireWriteLock` returns `NETCFG_E_ALREADY_INITIALIZED` (`0x8004A020`), which
is what `Initialize` returns on a second call. The returned lock pointer is
therefore resolving to the INetCfg vtable, not the lock vtable. `Install` then
fails with `NETCFG_E_NO_WRITE_LOCK` (`0x8004A024`).

That was resolved: XP takes the write lock **before** `Initialize`. With the
correct order, `AcquireWriteLock`, `Initialize`, `QueryNetCfgClass`,
`Install("ms_anemosnet")` and `Apply()` all succeed, after
`SetupCopyOEMInf` registers the INF. The component is really installed.

The first `Install` failure was `SPAPI_E_NO_DRIVER_SELECTED` because
`INetCfgClassSetup::Install` takes the **component id**, not the INF filename.

After a reboot, NDIS calls `ProtocolBindAdapter` (`ndis.calls` becomes 1), so
the installation path is correct. The bind itself still fails:
`NdisOpenAdapter` returns `0xC0010002` (`NDIS_STATUS_CLOSING`) even though the
adapter is up. Next: treat that as a transient, return `NDIS_STATUS_PENDING`
and complete later with `NdisCompleteBindAdapter` after a short delay, or find
why NDIS reports the binding as closing.

A stale-image trap cost several runs: the query phase boots the **installed**
driver, so it must reinstall the newly built image (netcfg does) before
testing; counters from a rebuild mean nothing until the image is replaced.

### Binding result

The last bug was the protocol handle. `ProtocolBindAdapter`'s `BindContext` is
not the handle `NdisOpenAdapter` wants as `NdisProtocolHandle`; passing the
handle returned by `NdisRegisterProtocol` makes the open succeed. The retry
path is kept for the transient closing state, but at boot the first open now
succeeds: `ndis.bound 1`, `ndis.bind_successes 1`, `ndis.calls 1`,
`ndis.attempts 0`. Certificate: `anemos/net/kernel/xp/anemos-k3-bind.txt`.

Frames are next: receive callback into lwIP, and an Ethernet netif backed by
`NdisSend`/`NdisSendPackets`.

The NIC itself is ready, which removes the hardware blocker.

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
