#!/usr/bin/env python3
"""Independent model of the DNS resolver glue.

It orchestrates two already-independent oracles: the Python socket model
(../winsock/socket_ref.py) and the Python DNS client model
(dns_client_ref.py). The C side orchestrates the C socket core and C client.
Agreement therefore tests the glue — ordering, retries, status mapping —
rather than re-testing either layer.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "winsock"))

import dns_client_ref as dc  # noqa: E402
import socket_ref  # noqa: E402

U32_MASK = 0xFFFFFFFF
U64_MASK = 0xFFFFFFFFFFFFFFFF
PORT = 53
TYPE_A = 1
TYPE_AAAA = 28

# The socket model is global, like the C socket core: a new Resolver after a
# `start` line mirrors anem_resolver_init and must not reset socket ids.
CORE = socket_ref.Core()

PENDING, OK, BADARG, DNS, TIMEOUT, TRANSPORT = (
    "pending", "ok", "badarg", "dns", "timeout", "transport")


def valid_name(text):
    if text == ".":
        return True                      # the C parser accepts the root name
    body = text[:-1] if text.endswith(".") else text
    if body == "":
        return False
    wire_len = 1
    for label in body.split("."):
        if label == "" or len(label) > 63:
            return False
        wire_len += 1 + len(label)
    return wire_len <= 255


def deadline_after(now, timeout):
    return min(U64_MASK, now + timeout)


class Resolver:
    def __init__(self, seed, attempts, timeout_ms):
        self.seed = seed & U32_MASK
        self.max_attempts = max(1, attempts & U32_MASK)
        self.timeout_ms = timeout_ms & U64_MASK
        self.core = CORE
        self.client = dc.Client(self.seed)
        self.sock = 0
        self.status = BADARG
        self.attempts = 0
        self.deadline = 0
        self.qtype = 0
        self.qname_wire = b""
        self.server = "0.0.0.0"
        self.result = None
        self.client_status = 0

    def cancel(self):
        self.close()
        self.status = BADARG
        return self.status

    def close(self):
        if self.sock:
            self.core.op_close(self.sock)
            self.sock = 0

    def open_and_send(self, query_len):
        ident = self.core.op_socket("dgram")
        if ident < 0:
            return TRANSPORT
        self.sock = ident
        if self.core.op_connect(ident, self.server, PORT) < 0:
            self.close()
            return TRANSPORT
        if self.core.op_send(ident, query_len) < 0:
            self.close()
            return TRANSPORT
        return PENDING

    def start(self, name, qtype_text, server, now):
        if qtype_text not in ("A", "AAAA") or not valid_name(name):
            return self.cancel()
        self.qtype = TYPE_A if qtype_text == "A" else TYPE_AAAA
        self.server = server
        self.qname_wire = dc.enc_name(name)
        self.close()
        self.client.pending = False       # abandon an in-flight lookup
        if self.client.query(self.qname_wire, self.qtype) != dc.OK:
            self.status = BADARG
            return self.status
        query_len = 12 + len(self.qname_wire) + 4
        self.attempts = 1
        self.deadline = deadline_after(now, self.timeout_ms)
        self.status = self.open_and_send(query_len)
        return self.status

    def on_packet(self, wire, now):
        if self.status != PENDING:
            return self.status
        status, result = self.client.receive(wire)
        self.client_status = status
        if status == dc.OK:
            self.status = OK
            self.result = result
            self.close()
        elif status in (dc.IGNORED, dc.MALFORMED):
            self.status = PENDING
        elif status in (dc.BADARG, dc.BADSTATE):
            self.status = BADARG
            self.close()
        else:
            self.status = DNS
            self.result = result
            self.close()
        return self.status

    def on_timer(self, now):
        if self.status != PENDING:
            return self.status
        if now < self.deadline:
            return self.status
        if self.attempts >= self.max_attempts:
            self.status = TIMEOUT
            self.close()
            return self.status
        self.attempts += 1
        self.close()
        query_len = 12 + len(self.qname_wire) + 4
        self.status = self.open_and_send(query_len)
        if self.status == PENDING:
            self.deadline = deadline_after(now, self.timeout_ms)
        return self.status

    def result_line(self):
        if self.status != OK or self.result is None:
            return f"result {self.status} - - -"
        family = self.result["family"]
        addr = self.result["addr"]
        text = (".".join(str(b) for b in addr) if family == 4 else addr.hex())
        return f"result ok {family} {text} {self.result['ttl']}"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: resolver_ref.py <trace>")
    resolver = Resolver(0, 1, 0)          # idle, exactly like the C probe
    with open(sys.argv[1], encoding="utf-8") as trace:
        for raw in trace:
            line = raw.rstrip("\r\n")
            if not line:
                continue
            fields = line.split("\t")
            command = fields[0]
            if command == "init" and len(fields) >= 4:
                resolver = Resolver(int(fields[1]), int(fields[2]),
                                    int(fields[3]))
                print(f"init {fields[1]} {fields[2]} {fields[3]}")
            elif command == "start" and len(fields) >= 5:
                name, qtype, server = fields[1], fields[2], fields[3]
                now = int(fields[4]) & U64_MASK
                try:
                    __import__("ipaddress").IPv4Address(server)
                    server_ok = True
                except Exception:
                    server_ok = False
                status = (resolver.start(name, qtype, server, now)
                          if server_ok else resolver.cancel())
                print(f"start {name} {qtype} {status}")
            elif command == "packet" and len(fields) >= 3:
                wire = bytes.fromhex(fields[1])
                now = int(fields[2]) & U64_MASK
                print(f"packet {resolver.on_packet(wire, now)}")
            elif command == "timer" and len(fields) >= 2:
                print(f"timer {resolver.on_timer(int(fields[1]) & U64_MASK)}")
            elif command == "status":
                print(f"status {resolver.status}")
            elif command == "result":
                print(resolver.result_line())
            elif command == "client":
                print(f"client {resolver.client_status}")
            else:
                print(f"? {command}")
            while resolver.core.requests:
                print(resolver.core.requests.pop(0))


if __name__ == "__main__":
    main()
