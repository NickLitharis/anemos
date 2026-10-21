#!/usr/bin/env bash
# Build the K1 driver and its embedded-image user-mode test.
#
# Order matters: XP verifies the driver's PE checksum at load time, and the
# test embeds the exact bytes it installs. Patch the checksum first, then
# generate the blob, then compile the test.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LLVM="$HOME/.local/opt/llvm-mingw-20261006-msvcrt-ubuntu-22.04-x86_64/bin"
OUT="$HERE/out"

"$HERE/build.sh"

python3 - "$OUT/anemosnet.sys" <<'PY'
import struct, sys
path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
pe = struct.unpack_from('<I', data, 0x3c)[0]
# Match XP's driver images: EXECUTABLE | LINE_NUMS_STRIPPED |
# LOCAL_SYMS_STRIPPED | 32BIT_MACHINE, and no DLL characteristics.
struct.pack_into('<H', data, pe + 4 + 0x12, 0x10E)
struct.pack_into('<H', data, pe + 0x18 + 0x46, 0x0)
off = pe + 0x58
struct.pack_into('<I', data, off, 0)
total = 0
for i in range(0, len(data), 2):
    word = data[i] | (data[i+1] << 8 if i+1 < len(data) else 0)
    total = (total + word) & 0xffffffff
    total = (total & 0xffff) + (total >> 16)
total = (total & 0xffff) + (total >> 16)
struct.pack_into('<I', data, off, (total + len(data)) & 0xffffffff)
open(path, 'wb').write(data)
PY

python3 - "$OUT/anemosnet.sys" "$OUT/driver_blob.h" <<'PY'
import sys
data = open(sys.argv[1], 'rb').read()
with open(sys.argv[2], 'w') as out:
    out.write("/* generated from anemosnet.sys */\n")
    out.write("static const unsigned char driver_blob[] = {\n")
    for i in range(0, len(data), 12):
        out.write("    " + ",".join(str(b) for b in data[i:i+12]) + ",\n")
    out.write("};\n")
PY

export PATH="$LLVM:$PATH"
i686-w64-mingw32-clang -std=c23 -Oz -Wall -Wextra -Werror \
    -I "$OUT" -o "$OUT/m1_test.exe" "$HERE/m1_test.c" -ladvapi32 \
    -Xlinker /subsystem:console,5.01
echo "built $OUT/m1_test.exe"
