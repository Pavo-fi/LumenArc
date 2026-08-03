# DMG (UDZO) 解压器：koly trailer -> blkx plist -> zlib chunks -> raw partition
import plistlib
import struct
import sys
import zlib

path, out = sys.argv[1], sys.argv[2]
data = open(path, 'rb').read()

# koly trailer: 最后 512 字节，magic 'koly'
trailer = data[-512:]
assert trailer[:4] == b'koly', 'not a koly dmg'
xml_off, xml_len = struct.unpack('>QQ', trailer[0xD8:0xE8])
plist = plistlib.loads(data[xml_off:xml_off + xml_len])

blocks = plist['resource-fork']['blkx']
print(f'blocks: {len(blocks)}')

total = 0
with open(out, 'wb') as f:
    for b in blocks:
        name = b.get('Name', '')
        raw = bytes(b['Data'])
        # mish header: sig(4) ver(4) sectorNum(8) sectorCnt(8) dataOff(8) buffersNeeded(4)
        #              blockDesc(4) cksumType(4) cksum(4*32) then run table
        assert raw[:4] == b'mish', f'bad mish in {name}'
        sector_num, sector_cnt = struct.unpack('>QQ', raw[8:24])
        nruns = struct.unpack('>I', raw[0xC8:0xCC])[0]
        # run table 起始: 0xCC（count 在 0xC8）
        pos = 0xCC
        sector_pos = sector_num
        out_off = sector_num * 512
        for i in range(nruns):
            rtype, _reserved, s_off, s_len, c_off, c_len = struct.unpack(
                '>IIQQQQ', raw[pos + i * 40: pos + i * 40 + 40])
            if rtype == 0x80000005:      # zlib
                chunk = zlib.decompress(data[c_off:c_off + c_len])
            elif rtype == 0x80000004:    # bz2 (不支持)
                import bz2
                chunk = bz2.decompress(data[c_off:c_off + c_len])
            elif rtype == 0x00000001:    # raw
                chunk = data[c_off:c_off + c_len]
            elif rtype in (0x00000000, 0x00000002, 0xFFFFFFFF):
                sector_pos += s_len
                out_off += s_len * 512
                continue
            else:
                raise ValueError(f'unknown run type {rtype:#x}')
            f.seek(out_off)
            f.write(chunk)
            out_off += len(chunk)
            sector_pos += s_len
        total = max(total, out_off)
print(f'done, raw size ~{total / 1e6:.0f} MB -> {out}')
