#!/usr/bin/env python3
"""Minimal read/write NTFS access inside a monolithicSparse VMDK. No root needed."""
import struct, sys

class Vmdk:
    def __init__(self, path, writable=False):
        """Open read-only unless `put` needs to write. A read-only image (a
        pristine oracle mounted ro, or a host filesystem that remounted ro
        after errors) can still be listed, stat'ed and extracted."""
        self.f = open(path, 'r+b' if writable else 'rb')
        hdr = self.f.read(512)
        if hdr[:4] != b'KDMV':
            raise SystemExit('not monolithicSparse')
        self.grainSize = struct.unpack('<Q', hdr[20:28])[0]
        gdOffset = struct.unpack('<Q', hdr[56:64])[0]
        self.numGTEs = struct.unpack('<I', hdr[44:48])[0]
        self.f.seek(gdOffset * 512)
        self.gd = list(struct.unpack('<%dI' % self.numGTEs, self.f.read(4 * self.numGTEs)))
        self._gtcache = {}

    def _gt(self, gde):
        if gde not in self._gtcache:
            self.f.seek(gde * 512)
            self._gtcache[gde] = list(struct.unpack('<512I', self.f.read(2048)))
        return self._gtcache[gde]

    def read_sectors(self, lba, n):
        out = bytearray(); gs = self.grainSize
        while n > 0:
            g = lba // gs; off = lba % gs; take = min(n, gs - off)
            gde = self.gd[g // 512]
            if gde == 0:
                out += b'\0' * (take * 512)
            else:
                go = self._gt(gde)[g % 512]
                if go == 0:
                    out += b'\0' * (take * 512)
                else:
                    self.f.seek(go * 512 + off * 512); out += self.f.read(take * 512)
            lba += take; n -= take
        return bytes(out)

    def write_sectors(self, lba, data):
        gs = self.grainSize; pos = 0; n = len(data) // 512
        while n > 0:
            g = lba // gs; off = lba % gs; take = min(n, gs - off)
            gde = self.gd[g // 512]
            if gde == 0: raise SystemExit('unallocated grain dir')
            gt = self._gt(gde)
            if gt[g % 512] == 0: raise SystemExit('unallocated grain at %d' % g)
            self.f.seek(gt[g % 512] * 512 + off * 512)
            self.f.write(data[pos:pos + take * 512])
            lba += take; pos += take * 512; n -= take
        self.f.flush()

class NTFS:
    def __init__(self, vmdk, part_lba):
        self.v = vmdk; self.base = part_lba
        boot = vmdk.read_sectors(part_lba, 1)
        if boot[3:11] != b'NTFS    ': raise SystemExit('not NTFS')
        self.bps = struct.unpack('<H', boot[11:13])[0]
        self.cluster = self.bps * boot[13]
        self.mft_lcn = struct.unpack('<Q', boot[48:56])[0]
        # read $MFT's own data runs to locate all MFT records
        self.mft_runs = self._mft_runs()
        self.mft_clusters = sum(n for _, n in self.mft_runs)

    def cl2lba(self, lcn): return self.base + lcn * self.cluster // 512

    def _runs(self, data):
        runs = []; cur = 0; i = 0
        while i < len(data):
            b = data[i]
            if b == 0: break
            lsz = b & 0xF; osz = b >> 4; i += 1
            ncl = int.from_bytes(data[i:i+lsz], 'little'); i += lsz
            d = int.from_bytes(data[i:i+osz], 'little', signed=True); i += osz
            cur += d; runs.append((cur, ncl))
        return runs

    def _attr(self, rec, atype):
        off = struct.unpack('<H', rec[20:22])[0]
        while off < len(rec) - 8:
            t = struct.unpack('<I', rec[off:off+4])[0]
            if t == 0xFFFFFFFF: return None
            ln = struct.unpack('<I', rec[off+4:off+8])[0]
            if ln == 0: return None
            if t == atype: return rec[off:off+ln]
            off += ln
        return None

    def _mft_runs(self):
        lba = self.cl2lba(self.mft_lcn)
        rec = self.v.read_sectors(lba, 2)
        a = self._attr(rec, 0x80)
        if a is None: raise SystemExit('no $MFT data')
        return self._runs(a[struct.unpack('<H', a[32:34])[0]:])

    def record(self, idx):
        """Read MFT record idx, following $MFT's run list.

        NTFS protects each record sector with an update-sequence array: the
        last two bytes of every sector hold a sequence number and the real
        bytes live in the USA. Without applying the fixup, a resident file
        whose attribute crosses a sector boundary reads back with two USN
        bytes in its data (found via a 593-byte certificate).
        """
        off = idx * 2  # sectors into the MFT
        for lcn, ncl in self.mft_runs:
            nsec = ncl * self.cluster // 512
            if off < nsec:
                rec = bytearray(self.v.read_sectors(self.cl2lba(lcn) + off, 2))
                usa_off = struct.unpack('<H', rec[4:6])[0]
                usa_cnt = struct.unpack('<H', rec[6:8])[0]
                for i in range(1, usa_cnt):
                    end = i * self.bps
                    if end <= len(rec) and usa_off + i * 2 + 2 <= len(rec):
                        rec[end-2:end] = rec[usa_off+i*2:usa_off+i*2+2]
                return bytes(rec)
            off -= nsec
        raise SystemExit('record %d beyond $MFT' % idx)

    def data(self, attr):
        """Return full attribute value bytes (resident or non-resident)."""
        if attr[8] == 0:
            vo = struct.unpack('<H', attr[20:22])[0]
            vl = struct.unpack('<I', attr[16:20])[0]
            return attr[vo:vo+vl]
        runs = self._runs(attr[struct.unpack('<H', attr[32:34])[0]:])
        real = struct.unpack('<Q', attr[48:56])[0]
        out = bytearray()
        for lcn, ncl in runs:
            out += self.v.read_sectors(self.cl2lba(lcn), ncl * self.cluster // 512)
        return bytes(out[:real])

    def fname(self, idx):
        rec = self.record(idx)
        a = self._attr(rec, 0x30)
        if a is None: return None
        val = self.data(a)
        nlen = val[64]
        return val[66:66+nlen*2].decode('utf-16-le', 'ignore')

    def listdir(self, idx):
        rec = self.record(idx)
        names = []
        # $INDEX_ROOT
        ir = self._attr(rec, 0x90)
        if ir is not None:
            val = self.data(ir)
            # the node header sits at 0x10 and its offset is relative to itself;
            # val[0:4] is the attribute type, not an offset — reading it as one
            # made every directory small enough to keep its entries resident
            # (a fresh one, e.g. \conftest) list as empty.
            ent_off = struct.unpack('<I', val[16:20])[0] + 16
            names += self._parse_index(val[ent_off:])
        ia = self._attr(rec, 0xA0)
        if ia is not None:
            buf = self.data(ia)
            for off in range(0, len(buf), self.cluster):
                blk = bytearray(buf[off:off+self.cluster])
                if blk[:4] != b'INDX':
                    continue
                usa_off = struct.unpack('<H', blk[4:6])[0]
                usa_cnt = struct.unpack('<H', blk[6:8])[0]
                for i in range(1, usa_cnt):
                    e = i * self.bps
                    if e <= len(blk):
                        blk[e-2:e] = blk[usa_off+i*2:usa_off+i*2+2]
                ent_off = struct.unpack('<I', blk[24:28])[0]
                names += self._parse_index(bytes(blk)[24+ent_off:])
        return names

    def _parse_index(self, data):
        """Parse a run of index entries. data starts at the first entry."""
        out = []
        o = 0
        while o + 16 <= len(data):
            ent_len = struct.unpack('<H', data[o+8:o+10])[0]
            if ent_len == 0 or o + ent_len > len(data):
                break
            val = data[o+16:o+ent_len]
            if len(val) > 66:
                nlen = val[64]
                nm = val[66:66+nlen*2].decode('utf-16-le', 'ignore')
                mref = struct.unpack('<Q', data[o:o+8])[0] & 0xFFFFFFFFFFFF
                if nm:
                    out.append((nm, mref))
            o += ent_len
        return out

    def lookup(self, path):
        parts = [p for p in path.strip('\\').split('\\') if p]
        idx = 5
        for part in parts:
            hit = None
            for nm, ref in self.listdir(idx):
                if nm.lower() == part.lower():
                    hit = ref; break
            if hit is None: raise SystemExit('not found: ' + part)
            idx = hit
        return idx

    def read_file(self, path):
        rec = self.record(self.lookup(path))
        a = self._attr(rec, 0x80)
        if a is None: raise SystemExit('no $DATA')
        return self.data(a)

    def file_stat(self, path):
        """Return (real size, allocated size, attribute flags) for a file.

        Flags expose FILE_ATTRIBUTE_COMPRESSED (0x0001): a raw overwrite of a
        compressed file would be stored as compressed data and read back as
        garbage, so callers choosing a victim must reject it.
        """
        rec = self.record(self.lookup(path))
        a = self._attr(rec, 0x80)
        if a is None: raise SystemExit('no $DATA')
        flags = struct.unpack('<H', a[12:14])[0]
        if a[8] == 0:
            vl = struct.unpack('<I', a[16:20])[0]
            return vl, vl, flags
        alloc = struct.unpack('<Q', a[40:48])[0]
        real = struct.unpack('<Q', a[48:56])[0]
        return real, alloc, flags

    def write_file(self, path, data):
        idx = self.lookup(path)
        rec = self.record(idx)
        a = self._attr(rec, 0x80)
        alloc = struct.unpack('<Q', a[40:48])[0]
        if len(data) > alloc:
            raise SystemExit('too big: %d > %d' % (len(data), alloc))
        runs = self._runs(a[struct.unpack('<H', a[32:34])[0]:])
        pos = 0
        for lcn, ncl in runs:
            chunk = min(ncl * self.cluster, len(data) - pos)
            if chunk <= 0: break
            self.v.write_sectors(self.cl2lba(lcn), data[pos:pos+chunk])
            pos += chunk
        print('wrote %d of %d alloc bytes' % (pos, alloc))


def _main():
    import os
    if len(sys.argv) < 3:
        print(__doc__ or 'usage: ntfspatch.py <vmdk> ls|get|put|stat [args]')
        print('  ls                        list NTFS root')
        print('  get <path> <out>          extract a file')
        print('  put <path> <in>           overwrite a file (must fit allocation)')
        print('  stat <path>               print real and allocated bytes')
        raise SystemExit(2)
    cmd = sys.argv[2]
    if cmd not in ('ls', 'get', 'put', 'stat'):
        raise SystemExit('unknown command: ' + cmd)
    # Only `put` writes; everything else works on a read-only image.
    v = Vmdk(sys.argv[1], writable=(cmd == 'put'))
    part = int(os.environ.get('PART', '1'))
    mbr = v.read_sectors(0, 1)
    off = 446 + (part - 1) * 16
    lba = struct.unpack('<I', mbr[off + 8:off + 12])[0]
    n = NTFS(v, lba)
    if cmd == 'ls':
        for name, ref in n.listdir(5):
            print('%-30s %d' % (name, ref))
    elif cmd == 'get':
        data = n.read_file(sys.argv[3])
        with open(sys.argv[4], 'wb') as f:
            f.write(data)
        print('extracted %d bytes' % len(data))
    elif cmd == 'stat':
        real, alloc, flags = n.file_stat(sys.argv[3])
        print('size %d alloc %d flags 0x%x' % (real, alloc, flags))
    elif cmd == 'put':
        with open(sys.argv[4], 'rb') as f:
            data = f.read()
        n.write_file(sys.argv[3], data)


if __name__ == '__main__':
    _main()
