#!/usr/bin/env python3
"""Independent model of the socket lifecycle core.

Written from the state rules, not from socket_core.c: the point is that two
implementations agree on a trace. Output text must match socket_probe.c
exactly (same canonical lines, same order, same request queue drain).
"""
import ipaddress
import sys

WSAEINVAL = 10022
WSAEWOULDBLOCK = 10035
WSAEALREADY = 10037
WSAENOTSOCK = 10038
WSAEOPNOTSUPP = 10045
WSAEAFNOSUPPORT = 10047
WSAEADDRINUSE = 10048
WSAEADDRNOTAVAIL = 10049
WSAENOBUFS = 10055
WSAEISCONN = 10056
WSAENOTCONN = 10057
WSAECONNREFUSED = 10061
WSAECONNRESET = 10054
WSAESHUTDOWN = 10058

MAX_SOCKETS = 64
SD_RECEIVE, SD_SEND, SD_BOTH = 0, 1, 2

CLOSED, CREATED, BOUND, LISTENING, CONNECTING, CONNECTED = (
    "closed", "created", "bound", "listening", "connecting", "connected")


class Slot:
    def __init__(self, ident, kind):
        self.id = ident
        self.type = kind
        self.state = CREATED
        self.addr = "0.0.0.0"
        self.port = 0
        self.backlog = 0
        self.pending = 0
        self.rx_available = 0
        self.writable = False
        self.eof = False
        self.reset = False
        self.error = 0
        self.shut_read = False
        self.shut_write = False


class Core:
    def __init__(self):
        self.slots = [None] * MAX_SOCKETS
        self.next_id = 1
        self.next_ephemeral = 49152
        self.requests = []

    def emit(self, kind, ident, *rest):
        self.requests.append(" ".join(["req", kind, str(ident)] + [str(x) for x in rest]))

    def slot(self, ident):
        for slot in self.slots:
            if slot is not None and slot.id == ident:
                return slot
        return None

    def free_slot(self):
        for index, slot in enumerate(self.slots):
            if slot is None:
                return index
        return None

    def ephemeral(self, kind):
        for _ in range(16384):
            port = self.next_ephemeral
            self.next_ephemeral = (49152 if self.next_ephemeral >= 65535
                                   else self.next_ephemeral + 1)
            if not self.port_in_use(port, kind):
                return port
        return None

    def port_in_use(self, port, kind):
        if port == 0:
            return False
        for slot in self.slots:
            if slot is None or slot.state == CREATED:
                continue
            if slot.port == port and slot.type == kind:
                return True
        return False

    def op_socket(self, kind):
        if kind not in ("stream", "dgram"):
            return -WSAEINVAL
        index = self.free_slot()
        if index is None:
            return -WSAENOBUFS
        ident = self.next_id
        self.next_id += 1
        self.slots[index] = Slot(ident, kind)
        self.emit("open", ident, 1 if kind == "stream" else 2)
        return ident

    def op_bind(self, ident, addr, port):
        slot = self.slot(ident)
        if slot is None:
            return -WSAENOTSOCK
        if slot.state != CREATED:
            return -WSAEINVAL
        if addr == "255.255.255.255":
            return -WSAEADDRNOTAVAIL
        if port == 0:
            port = self.ephemeral(slot.type)
            if port is None:
                return -WSAEADDRNOTAVAIL
        elif self.port_in_use(port, slot.type):
            return -WSAEADDRINUSE
        slot.addr = addr
        slot.port = port
        slot.state = BOUND
        self.emit("bind", ident, addr, port)
        return 0

    def op_listen(self, ident, backlog):
        slot = self.slot(ident)
        if slot is None:
            return -WSAENOTSOCK
        if slot.type != "stream":
            return -WSAEINVAL
        if slot.state not in (BOUND, LISTENING):
            return -WSAEINVAL
        slot.backlog = backlog if backlog else 1
        slot.state = LISTENING
        self.emit("listen", ident, slot.backlog)
        return 0

    def op_connect(self, ident, addr, port):
        slot = self.slot(ident)
        if slot is None:
            return -WSAENOTSOCK
        if slot.state == CONNECTING:
            return -WSAEALREADY
        if slot.state == CONNECTED:
            return -WSAEISCONN
        if slot.state == LISTENING:
            return -WSAEINVAL
        slot.error = 0
        if slot.state == CREATED:
            local = self.ephemeral(slot.type)
            if local is None:
                return -WSAEADDRNOTAVAIL
            slot.addr = "0.0.0.0"
            slot.port = local
            slot.state = BOUND
            self.emit("bind", ident, slot.addr, slot.port)
        slot.state = CONNECTED if slot.type == "dgram" else CONNECTING
        if slot.state == CONNECTED:
            slot.writable = True
        self.emit("connect", ident, addr, port)
        return 0

    def op_accept(self, ident):
        listener = self.slot(ident)
        if listener is None:
            return -WSAENOTSOCK
        if listener.state != LISTENING:
            return -WSAEINVAL
        if listener.pending == 0:
            return -WSAEWOULDBLOCK
        index = self.free_slot()
        if index is None:
            return -WSAENOBUFS
        listener.pending -= 1
        new_id = self.next_id
        self.next_id += 1
        slot = Slot(new_id, listener.type)
        slot.state = CONNECTED
        slot.writable = True
        self.slots[index] = slot
        self.emit("accept", new_id)
        return new_id

    def op_shutdown(self, ident, how):
        slot = self.slot(ident)
        if slot is None:
            return -WSAENOTSOCK
        if slot.state != CONNECTED:
            return -WSAENOTCONN
        if how not in (SD_RECEIVE, SD_SEND, SD_BOTH):
            return -WSAEINVAL
        if ((how in (SD_RECEIVE, SD_BOTH) and slot.shut_read)
                or (how in (SD_SEND, SD_BOTH) and slot.shut_write)):
            return -WSAENOTCONN
        if how in (SD_RECEIVE, SD_BOTH):
            slot.shut_read = True
            slot.rx_available = 0
        if how in (SD_SEND, SD_BOTH):
            slot.shut_write = True
            slot.writable = False
        self.emit("shutdown", ident, how)
        return 0

    def op_close(self, ident):
        slot = self.slot(ident)
        if slot is None:
            return -WSAENOTSOCK
        self.emit("close", ident)
        self.slots[self.slots.index(slot)] = None
        return 0

    def event_connected(self, ident):
        slot = self.slot(ident)
        if slot is not None and slot.state == CONNECTING:
            slot.state = CONNECTED
            slot.writable = True
            slot.error = 0

    def event_refused(self, ident):
        slot = self.slot(ident)
        if slot is not None and slot.state == CONNECTING:
            slot.state = BOUND
            slot.error = WSAECONNREFUSED
            slot.writable = True

    def op_send(self, ident, length):
        slot = self.slot(ident)
        if slot is None:
            return -WSAENOTSOCK
        if slot.state != CONNECTED:
            return -WSAENOTCONN
        if slot.reset:
            return -WSAECONNRESET
        if slot.shut_write:
            return -WSAESHUTDOWN
        if length == 0:
            return 0
        if not slot.writable:
            return -WSAEWOULDBLOCK
        slot.writable = False
        self.emit("send", ident, length)
        return 0

    def op_recv(self, ident, length):
        if self.slot(ident) is None:
            return -WSAENOTSOCK, 0
        slot = self.slot(ident)
        if slot.state != CONNECTED:
            return -WSAENOTCONN, 0
        if slot.reset:
            return -WSAECONNRESET, 0
        if slot.shut_read:
            return -WSAESHUTDOWN, 0
        if length == 0:
            return 0, 0
        take = min(length, slot.rx_available)
        if take > 0:
            slot.rx_available -= take
            return 0, take
        if slot.eof:
            return 0, 0
        return -WSAEWOULDBLOCK, 0

    def op_error(self, ident):
        slot = self.slot(ident)
        return -WSAENOTSOCK if slot is None else slot.error

    def op_select(self, read_ids, write_ids):
        for ident in read_ids + write_ids:
            if self.slot(ident) is None:
                return -WSAENOTSOCK, [], []
        read_ready = []
        write_ready = []
        ready = 0
        for ident in read_ids:
            slot = self.slot(ident)
            is_ready = ((slot.state == LISTENING and slot.pending > 0)
                        or slot.rx_available > 0 or slot.eof or slot.reset
                        or slot.shut_read)
            read_ready.append(is_ready)
            ready += is_ready
        for ident in write_ids:
            slot = self.slot(ident)
            is_ready = (slot.writable or slot.reset or slot.error
                        or slot.shut_write)
            write_ready.append(is_ready)
            ready += is_ready
        return ready, read_ready, write_ready

    def event_readable(self, ident, count):
        slot = self.slot(ident)
        if slot is None or slot.state != CONNECTED or slot.reset or slot.eof:
            return
        if not slot.shut_read:
            slot.rx_available = min(slot.rx_available + count, 0xFFFFFFFF)

    def event_writable(self, ident):
        slot = self.slot(ident)
        if (slot is not None and slot.state == CONNECTED
                and not slot.shut_write and not slot.reset):
            slot.writable = True

    def event_eof(self, ident):
        slot = self.slot(ident)
        if slot is not None and slot.state == CONNECTED:
            slot.eof = True

    def event_reset(self, ident):
        slot = self.slot(ident)
        if slot is None:
            return
        if slot.state == CONNECTED:
            slot.reset = True
            slot.rx_available = 0
        elif slot.state == CONNECTING:
            slot.state = BOUND
            slot.error = WSAECONNRESET
            slot.writable = True

    def event_incoming(self, ident):
        slot = self.slot(ident)
        if slot is None or slot.state != LISTENING:
            return
        if slot.pending < slot.backlog:
            slot.pending += 1
        else:
            self.emit("refuse", ident)


def parse_address(text):
    try:
        return str(ipaddress.IPv4Address(text))
    except (ipaddress.AddressValueError, ValueError):
        return None


def parse_uint(text, limit=65535):
    if text == "" or not text.isdigit():
        return None
    value = int(text)
    return value if value <= limit else None


def parse_id(text):
    body = text[1:] if text.startswith("-") else text
    if body == "" or not body.isdigit():
        return -1
    value = int(text)
    return value if abs(value) <= 1000000 else -1


def parse_id_list(text):
    if text == "":
        return []
    ids = []
    for part in text.split(","):
        if part == "" or not part.isdigit() or len(ids) >= MAX_SOCKETS:
            return None
        value = int(part)
        if value > 1000000:
            return None
        ids.append(value)
    return ids


def bits(ready):
    return "-" if not ready else "".join("1" if x else "0" for x in ready)


def run(core, fields):
    command = fields[0]
    if command == "socket" and len(fields) >= 2:
        result = core.op_socket(fields[1])
        print(f"socket {fields[1]} {result}")
    elif command == "bind" and len(fields) >= 4:
        address = parse_address(fields[2])
        port = parse_uint(fields[3])
        result = (-WSAEINVAL if address is None or port is None
                  else core.op_bind(parse_id(fields[1]), address, port))
        print(f"bind {fields[1]} {fields[2]} {fields[3]} {result}")
    elif command == "listen" and len(fields) >= 3:
        backlog = parse_uint(fields[2])
        result = (-WSAEINVAL if backlog is None
                  else core.op_listen(parse_id(fields[1]), backlog))
        print(f"listen {fields[1]} {fields[2]} {result}")
    elif command == "connect" and len(fields) >= 4:
        address = parse_address(fields[2])
        port = parse_uint(fields[3])
        result = (-WSAEINVAL if address is None or port is None
                  else core.op_connect(parse_id(fields[1]), address, port))
        print(f"connect {fields[1]} {fields[2]} {fields[3]} {result}")
    elif command == "accept" and len(fields) >= 2:
        print(f"accept {fields[1]} {core.op_accept(parse_id(fields[1]))}")
    elif command == "shutdown" and len(fields) >= 3:
        how = parse_uint(fields[2])
        result = (-WSAEINVAL if how is None
                  else core.op_shutdown(parse_id(fields[1]), how))
        print(f"shutdown {fields[1]} {fields[2]} {result}")
    elif command == "close" and len(fields) >= 2:
        print(f"close {fields[1]} {core.op_close(parse_id(fields[1]))}")
    elif command == "state" and len(fields) >= 2:
        slot = core.slot(parse_id(fields[1]))
        print(f"state {fields[1]} {slot.state if slot else CLOSED}")
    elif command == "connected" and len(fields) >= 2:
        core.event_connected(parse_id(fields[1]))
        print(f"connected {fields[1]}")
    elif command == "refused" and len(fields) >= 2:
        core.event_refused(parse_id(fields[1]))
        print(f"refused {fields[1]}")
    elif command == "incoming" and len(fields) >= 2:
        core.event_incoming(parse_id(fields[1]))
        print(f"incoming {fields[1]}")
    elif command == "send" and len(fields) >= 3:
        length = parse_uint(fields[2])
        result = (-WSAEINVAL if length is None
                  else core.op_send(parse_id(fields[1]), length))
        print(f"send {fields[1]} {fields[2]} {result}")
    elif command == "recv" and len(fields) >= 3:
        length = parse_uint(fields[2])
        if length is None:
            result, received = -WSAEINVAL, 0
        else:
            result, received = core.op_recv(parse_id(fields[1]), length)
        print(f"recv {fields[1]} {fields[2]} {result} {received}")
    elif command == "readable" and len(fields) >= 3:
        count = parse_uint(fields[2])
        if count is not None:
            core.event_readable(parse_id(fields[1]), count)
        print(f"readable {fields[1]} {fields[2]}")
    elif command == "writable" and len(fields) >= 2:
        core.event_writable(parse_id(fields[1]))
        print(f"writable {fields[1]}")
    elif command == "eof" and len(fields) >= 2:
        core.event_eof(parse_id(fields[1]))
        print(f"eof {fields[1]}")
    elif command == "reset" and len(fields) >= 2:
        core.event_reset(parse_id(fields[1]))
        print(f"reset {fields[1]}")
    elif command == "select" and len(fields) >= 2:
        read_ids = parse_id_list(fields[1])
        write_ids = parse_id_list(fields[2]) if len(fields) >= 3 else []
        if read_ids is None or write_ids is None:
            print(f"select {-WSAEINVAL}")
        else:
            result, read_ready, write_ready = core.op_select(read_ids, write_ids)
            if result >= 0:
                print(f"select {result} {bits(read_ready)} {bits(write_ready)}")
            else:
                print(f"select {result}")
    elif command == "error" and len(fields) >= 2:
        print(f"error {fields[1]} {core.op_error(parse_id(fields[1]))}")
    else:
        print(f"? {command}")
    while core.requests:
        print(core.requests.pop(0))


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: socket_ref.py <trace>")
    core = Core()
    with open(sys.argv[1], encoding="utf-8") as trace:
        for raw in trace:
            line = raw.rstrip("\r\n")
            if not line:
                continue
            run(core, line.split("\t"))


if __name__ == "__main__":
    main()
