#!/usr/bin/env python3
"""Independent model of the DHCP client state machine.

This is the second implementation for the client, written from the behaviour
documented in dhcp_client.h and RFC 2131 §4.4. The differential gate feeds a
trace to both this model and the C client and requires identical state
snapshots and identical emitted packets after every operation.

usage:
  python3 dhcp_client_ref.py --check        run a few traces against itself
  python3 dhcp_client_ref.py --emit DIR     write traces/ and expected.txt
  python3 dhcp_client_ref.py --trace FILE   print the snapshot for a trace
"""

from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import dhcp_ref

# Mirrors AnemDhcpStatus / AnemDhcpState.
OK = 0
BADARG = 1
SHORT = 3
BAD_COOKIE = 4
BAD_HLEN = 5
BAD_OPTION = 6
NO_END = 7
TRAILING = 8
TOO_MANY = 9
NOSPACE = 10
BADSTATE = 13

INIT = 0
SELECTING = 1
REQUESTING = 2
BOUND = 3
RENEWING = 4
REBINDING = 5

RETRANSMIT_MS = 4000
RETRANSMIT_MAX_MS = 64000
RENEW_MIN_MS = 60000
UINT64_MAX = (1 << 64) - 1


def add_ms(a: int, b: int) -> int:
    return min(UINT64_MAX, a + b)


def backoff_ms(attempts: int) -> int:
    if attempts <= 1:
        return RETRANSMIT_MS
    if attempts >= 5:
        return RETRANSMIT_MAX_MS
    return RETRANSMIT_MS << (attempts - 1)


def renew_interval_ms(deadline: int, now: int) -> int:
    half = (deadline - now) // 2 if deadline > now else 0
    return max(RENEW_MIN_MS, half)


def get_opt(msg: dhcp_ref.Message, code: int) -> bytes | None:
    for opt in msg.options:
        if opt.code == code:
            return msg.wire[opt.off + 2 : opt.off + 2 + opt.length]
    return None


def opt_u32(msg: dhcp_ref.Message, code: int) -> int | None:
    data = get_opt(msg, code)
    if data is None or len(data) != 4:
        return None
    return struct.unpack(">I", data)[0]


@dataclass
class Client:
    seed: int
    chaddr: bytes
    hlen: int
    state: int = INIT
    htype: int = 1
    xid: int = 0
    offered_ip: int = 0
    ip: int = 0
    server_id: int = 0
    lease_secs: int = 0
    t1_secs: int = 0
    t2_secs: int = 0
    next_ms: int = 0
    t1_at_ms: int = 0
    t2_at_ms: int = 0
    lease_at_ms: int = 0
    attempts: int = 0

    def next_xid(self) -> int:
        self.seed = (self.seed * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.seed

    def _header(self, flags: int, ciaddr: int) -> dhcp_ref.Header:
        return dhcp_ref.Header(
            op=1, htype=1, hlen=self.hlen, hops=0, xid=self.xid,
            secs=0, flags=flags, ciaddr=ciaddr, yiaddr=0, siaddr=0,
            giaddr=0, chaddr=self.chaddr,
            sname=b"\x00" * 64, file=b"\x00" * 128,
        )

    def emit_discover(self) -> bytes:
        opts = bytes((53, 1, 1, 55, 3, 1, 3, 6, 61, 1 + self.hlen, 1))
        opts += self.chaddr[: self.hlen]
        return dhcp_ref.assemble(self._header(0x8000, 0), opts)

    def emit_request(self, selecting: bool, broadcast: bool) -> bytes:
        opts = bytes((53, 1, 3))
        if selecting:
            opts += bytes((50, 4)) + struct.pack(">I", self.offered_ip)
            opts += bytes((54, 4)) + struct.pack(">I", self.server_id)
        opts += bytes((61, 1 + self.hlen, 1)) + self.chaddr[: self.hlen]
        return dhcp_ref.assemble(
            self._header(0x8000 if broadcast else 0, 0 if selecting else self.ip),
            opts,
        )

    def restart(self, now: int) -> tuple[int, bytes | None]:
        self.xid = self.next_xid()
        packet = self.emit_discover()
        self.state = SELECTING
        self.attempts = 1
        self.next_ms = add_ms(now, RETRANSMIT_MS)
        self.offered_ip = 0
        self.ip = 0
        self.server_id = 0
        self.lease_secs = 0
        self.t1_secs = 0
        self.t2_secs = 0
        self.t1_at_ms = 0
        self.t2_at_ms = 0
        self.lease_at_ms = 0
        return OK, packet

    def start(self, now: int) -> tuple[int, bytes | None]:
        if self.state != INIT:
            return BADSTATE, None
        return self.restart(now)

    def apply_lease(self, msg: dhcp_ref.Message, now: int) -> bool:
        lease = opt_u32(msg, 51)
        if lease is None or lease == 0:
            return False
        t1 = opt_u32(msg, 58)
        if t1 is None or t1 == 0 or t1 >= lease:
            t1 = lease // 2
        t2 = opt_u32(msg, 59)
        if t2 is None or t2 <= t1 or t2 >= lease:
            t2 = lease * 7 // 8
        self.lease_secs = lease
        self.t1_secs = t1
        self.t2_secs = t2
        self.t1_at_ms = add_ms(now, t1 * 1000)
        self.t2_at_ms = add_ms(now, t2 * 1000)
        self.lease_at_ms = add_ms(now, lease * 1000)
        self.next_ms = self.t1_at_ms
        self.attempts = 0
        return True

    def receive(self, now: int, wire: bytes) -> tuple[int, bytes | None]:
        try:
            msg = dhcp_ref.parse(wire)
        except dhcp_ref.DhcpError as exc:
            return exc.code, None
        h = msg.header
        if (h.op != 2 or h.xid != self.xid or h.hlen != self.hlen
                or h.chaddr[: self.hlen] != self.chaddr[: self.hlen]):
            return OK, None
        type_data = get_opt(msg, 53)
        server = opt_u32(msg, 54)
        if type_data is None or len(type_data) != 1 or server is None:
            return OK, None
        mtype = type_data[0]
        yiaddr = h.yiaddr

        if self.state == SELECTING and mtype == 2 and yiaddr != 0:
            self.offered_ip = yiaddr
            self.server_id = server
            packet = self.emit_request(True, True)
            self.state = REQUESTING
            self.attempts = 1
            self.next_ms = add_ms(now, RETRANSMIT_MS)
            return OK, packet

        if mtype == 6 and self.state in (REQUESTING, RENEWING, REBINDING):
            return self.restart(now)

        if mtype != 5:
            return OK, None

        if self.state == REQUESTING:
            if server != self.server_id or yiaddr != self.offered_ip:
                return OK, None
            if not self.apply_lease(msg, now):
                return OK, None
            self.ip = yiaddr
            self.state = BOUND
            return OK, None
        if self.state == RENEWING:
            if server != self.server_id or not self.apply_lease(msg, now):
                return OK, None
            if yiaddr != 0:
                self.ip = yiaddr
            self.state = BOUND
            return OK, None
        if self.state == REBINDING:
            if not self.apply_lease(msg, now):
                return OK, None
            if yiaddr != 0:
                self.ip = yiaddr
            self.server_id = server
            self.state = BOUND
            return OK, None
        return OK, None

    def tick(self, now: int) -> tuple[int, bytes | None]:
        if self.state == INIT:
            return OK, None
        if self.state == SELECTING:
            if now < self.next_ms:
                return OK, None
            packet = self.emit_discover()
            self.attempts += 1
            self.next_ms = add_ms(now, backoff_ms(self.attempts))
            return OK, packet
        if self.state == REQUESTING:
            if now < self.next_ms:
                return OK, None
            packet = self.emit_request(True, True)
            self.attempts += 1
            self.next_ms = add_ms(now, backoff_ms(self.attempts))
            return OK, packet
        if self.state == BOUND:
            if now < self.t1_at_ms:
                return OK, None
            packet = self.emit_request(False, False)
            self.state = RENEWING
            self.attempts = 1
            self.next_ms = add_ms(now, renew_interval_ms(self.t2_at_ms, now))
            return OK, packet
        if self.state == RENEWING:
            if now >= self.lease_at_ms:
                return self.restart(now)
            if now >= self.t2_at_ms:
                packet = self.emit_request(False, True)
                self.state = REBINDING
                self.attempts = 1
                self.next_ms = add_ms(now, renew_interval_ms(self.lease_at_ms, now))
                return OK, packet
            if now >= self.next_ms:
                packet = self.emit_request(False, False)
                self.attempts += 1
                self.next_ms = add_ms(now, renew_interval_ms(self.t2_at_ms, now))
                return OK, packet
            return OK, None
        if self.state == REBINDING:
            if now >= self.lease_at_ms:
                return self.restart(now)
            if now >= self.next_ms:
                packet = self.emit_request(False, True)
                self.attempts += 1
                self.next_ms = add_ms(now, renew_interval_ms(self.lease_at_ms, now))
                return OK, packet
            return OK, None
        return BADSTATE, None


# ---- trace handling -------------------------------------------------------

def snapshot(i: int, status: int, cl: Client, packet: bytes | None) -> str:
    hexed = packet.hex() if packet is not None else "-"
    return (f"{i} {status} {cl.state} {cl.xid} {cl.offered_ip} {cl.ip} "
            f"{cl.server_id} {cl.lease_secs} {cl.t1_secs} {cl.t2_secs} "
            f"{cl.next_ms} {cl.t1_at_ms} {cl.t2_at_ms} {cl.lease_at_ms} "
            f"{cl.attempts} {hexed}")


def run_trace(lines: list[str]) -> list[str]:
    cl: Client | None = None
    out: list[str] = []
    for i, line in enumerate(lines):
        parts = line.split()
        if not parts:
            continue
        op = parts[0]
        status, packet = OK, None
        if op == "init":
            seed = int(parts[1], 0)
            chaddr = bytes.fromhex(parts[2])
            cl = Client(seed=seed, chaddr=chaddr, hlen=6)
        elif op == "start":
            assert cl is not None
            status, packet = cl.start(int(parts[1]))
        elif op == "tick":
            assert cl is not None
            status, packet = cl.tick(int(parts[1]))
        elif op == "recv":
            assert cl is not None
            status, packet = cl.receive(int(parts[1]), bytes.fromhex(parts[2]))
        else:
            raise SystemExit(f"bad trace operation: {op}")
        assert cl is not None
        out.append(snapshot(i, status, cl, packet))
    return out


# ---- server-side messages for the generated traces ------------------------

MAC = bytes.fromhex("000b8201fc42") + b"\x00" * 10
IP = 0xC0A80164
SERVER = 0xC0A80101
SERVER2 = 0xC0A80102


def make_reply(xid: int, mtype: int, *, op: int = 2, yiaddr: int = IP,
               server: int = SERVER, lease: int | None = None,
               t1: int | None = None, t2: int | None = None,
               chaddr: bytes = MAC, hlen: int = 6,
               with_server: bool = True) -> bytes:
    h = dhcp_ref.Header(op=op, htype=1, hlen=hlen, hops=0, xid=xid,
                        secs=0, flags=0, ciaddr=0, yiaddr=yiaddr,
                        siaddr=server, giaddr=0, chaddr=chaddr,
                        sname=b"\x00" * 64, file=b"\x00" * 128)
    opts = bytes((53, 1, mtype))
    if with_server:
        opts += bytes((54, 4)) + struct.pack(">I", server)
    if lease is not None:
        opts += bytes((51, 4)) + struct.pack(">I", lease)
    if t1 is not None:
        opts += bytes((58, 4)) + struct.pack(">I", t1)
    if t2 is not None:
        opts += bytes((59, 4)) + struct.pack(">I", t2)
    return dhcp_ref.assemble(h, opts)


def make_trace(seed: int, lease: int, t1: int | None, t2: int | None) -> list[str]:
    """Build a trace by running the model, so replies carry the right xid."""
    cl = Client(seed=seed, chaddr=MAC, hlen=6)
    lines = [f"init {seed} {MAC.hex()}"]
    lines.append("start 0")
    _, _ = cl.start(0)
    lines.append("tick 1000")
    cl.tick(1000)
    lines.append("tick 4000")
    cl.tick(4000)
    lines.append(f"recv 5000 {make_reply(cl.xid, 2).hex()}")
    cl.receive(5000, make_reply(cl.xid, 2))
    lines.append("tick 8999")
    cl.tick(8999)
    lines.append("tick 9000")
    cl.tick(9000)
    # A malformed frame and a wrong-xid OFFER are ignored on the way.
    lines.append("recv 9500 " + ("00" * 239))
    cl.receive(9500, b"\x00" * 239)
    lines.append(f"recv 9600 {make_reply(cl.xid ^ 0xFFFF, 2).hex()}")
    cl.receive(9600, make_reply(cl.xid ^ 0xFFFF, 2))
    ack = make_reply(cl.xid, 5, lease=lease, t1=t1, t2=t2)
    lines.append(f"recv 10000 {ack.hex()}")
    cl.receive(10000, ack)
    if cl.state != BOUND or cl.lease_secs != lease:
        # The ACK was deliberately ignored (wrong width etc.); still exercise
        # the remaining ticks from REQUESTING.
        pass
    t1_at = cl.t1_at_ms
    t2_at = cl.t2_at_ms
    lease_at = cl.lease_at_ms
    for label, t in (("t1-1", t1_at - 1), ("t1", t1_at),
                     ("t1+60000", t1_at + 60000)):
        lines.append(f"tick {t}")
        cl.tick(t)
    if cl.state == RENEWING:
        # Renewal ACK from the same server, then a second lease.
        reack = make_reply(cl.xid, 5, server=SERVER, lease=lease,
                           t1=t1, t2=t2)
        lines.append(f"recv {t1_at + 120000} {reack.hex()}")
        cl.receive(t1_at + 120000, reack)
        if cl.state == BOUND:
            # Wrong-server ACK during the next renewal must be ignored.
            lines.append(f"tick {cl.t1_at_ms}")
            cl.tick(cl.t1_at_ms)
            bad = make_reply(cl.xid, 5, server=SERVER2, lease=lease)
            lines.append(f"recv {cl.t1_at_ms + 1} {bad.hex()}")
            cl.receive(cl.t1_at_ms + 1, bad)
            if cl.state == RENEWING:
                lines.append(f"tick {cl.t2_at_ms}")
                cl.tick(cl.t2_at_ms)
            # Rebind ACK from another server.
            if cl.state == REBINDING:
                rback = make_reply(cl.xid, 5, server=SERVER2, lease=lease)
                lines.append(f"recv {cl.t2_at_ms + 1} {rback.hex()}")
                cl.receive(cl.t2_at_ms + 1, rback)
    if cl.state in (BOUND, RENEWING, REBINDING):
        expiry = cl.lease_at_ms
        lines.append(f"tick {expiry}")
        cl.tick(expiry)
        lines.append("tick " + str(expiry + 1000))
        cl.tick(expiry + 1000)
    # NAK the fresh transaction (or the current one) and restart.
    if cl.state == SELECTING:
        lines.append(f"recv {cl.next_ms} {make_reply(cl.xid, 2).hex()}")
        cl.receive(cl.next_ms, make_reply(cl.xid, 2))
    if cl.state == REQUESTING:
        nak = make_reply(cl.xid, 6, yiaddr=0)
        lines.append(f"recv {cl.next_ms} {nak.hex()}")
        cl.receive(cl.next_ms, nak)
    # Let the new DISCOVER retransmit twice.
    lines.append(f"tick {cl.next_ms}")
    cl.tick(cl.next_ms)
    lines.append(f"tick {cl.next_ms}")
    cl.tick(cl.next_ms)
    _ = (t1_at, t2_at, lease_at)
    return lines


def emit(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    traces: list[list[str]] = []
    for i, (lease, t1, t2) in enumerate((
        (3600, None, None),
        (100, None, None),
        (30, None, None),
        (300, 60, 240),
        (600, 100, 500),
        (3600, 1000, 2000),
    )):
        traces.append(make_trace(1 + i * 17, lease, t1, t2))
    expected: list[str] = []
    for i, trace in enumerate(traces):
        (directory / f"trace{i}.txt").write_text("\n".join(trace) + "\n",
                                                 encoding="ascii")
        expected.extend(run_trace(trace))
    (directory / "expected.txt").write_text("\n".join(expected) + "\n",
                                            encoding="ascii")
    print(f"client traces: {len(traces)}, expected lines: {len(expected)}")


def check() -> int:
    failures = 0
    for i, (lease, t1, t2) in enumerate((
        (3600, None, None), (100, None, None), (300, 60, 240)
    )):
        trace = make_trace(1 + i * 17, lease, t1, t2)
        lines = run_trace(trace)
        if not lines:
            print(f"FAIL trace {i}: no output")
            failures += 1
            continue
        # Every emitted packet must parse.
        for line in lines:
            fields = line.split()
            if fields[-1] != "-":
                if dhcp_ref.classify(bytes.fromhex(fields[-1])) != dhcp_ref.OK:
                    print(f"FAIL trace {i}: emitted packet does not parse")
                    failures += 1
                    break
        print(f"PASS trace {i} ({len(lines)} operations)")
    print(f"{failures} failure(s)")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--check":
        return check()
    if len(argv) == 3 and argv[1] == "--emit":
        emit(Path(argv[2]))
        return 0
    if len(argv) == 3 and argv[1] == "--trace":
        lines = Path(argv[2]).read_text(encoding="ascii").splitlines()
        print("\n".join(run_trace(lines)))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
