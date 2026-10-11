import struct, sys

def parse(path):
    data = open(path, 'rb').read()
    tag = data[:4]
    if tag in (b'ttcf', b'TTCF'):
        # TTC: read offset table
        num = struct.unpack('>I', data[12:16])[0]
        base = struct.unpack('>I', data[16:20])[0]
        print(f'TTC with {num} fonts, first offset={base}')
        data = data[base:]
    if data[:4] not in (b'\x00\x01\x00\x00', b'OTTO', b'true', b'ttcf'):
        print('unknown sfnt tag', data[:4])
        return None
    num_tables = struct.unpack('>H', data[4:6])[0]
    tables = {}
    for i in range(num_tables):
        off = 12 + 16 * i
        t = data[off:off+4]
        o, l = struct.unpack('>II', data[off+8:off+16])
        tables[t] = (o, l)
    print('tables:', sorted(t.decode() for t in tables))
    if b'cmap' not in tables:
        return None
    co, cl = tables[b'cmap']
    cmap = data[co:co+cl]
    n = struct.unpack('>H', cmap[2:4])[0]
    best = None
    for i in range(n):
        pid, eid, off = struct.unpack('>HHI', cmap[4 + 8*i:12 + 8*i])
        print(f'  subtable pid={pid} eid={eid} off={off}')
        if (pid, eid) in (3, 10) or (pid, eid) == (3, 1):
            best = off
    if best is None:
        return None
    fmt = struct.unpack('>H', cmap[best:best+2])[0]
    chars = set()
    if fmt == 4:
        segx2 = struct.unpack('>H', cmap[best+6:best+8])[0]
        segc = segx2 // 2
        ends = struct.unpack('>' + 'H'*segc, cmap[best+14:best+14+segx2])
        startp = best + 16 + segx2
        starts = struct.unpack('>' + 'H'*segc, cmap[startp:startp+segx2])
        delta = struct.unpack('>' + 'h'*segc, cmap[startp+segx2:startp+2*segx2])
        rangep = startp + 2*segx2
        ranges = struct.unpack('>' + 'H'*segc, cmap[rangep:rangep+segx2])
        for i in range(segc):
            for c in range(starts[i], min(ends[i], 0xFFFF) + 1):
                if c == 0xFFFF:
                    continue
                g = (ranges[i] + delta[i] + c) & 0xFFFF if ranges[i] != 0 else 0
                if g:
                    chars.add(c)
    elif fmt == 12:
        ngroups = struct.unpack('>I', cmap[best+12:best+16])[0]
        for i in range(ngroups):
            s, e, gi = struct.unpack('>III', cmap[best+16+12*i:best+28+12*i])
            for c in range(s, e+1):
                chars.add(c)
    return chars

for path in sys.argv[1:]:
    ch = parse(path)
    if ch is None:
        print(path, '-> no cmap parsed')
        continue
    test = '屏幕使用时间锁休息密码取消确定分钟今日剩余已用家长设置日志配置重载退出'
    missing = [c for c in test if ord(c) not in ch]
    cjk = sum(1 for c in ch if 0x4E00 <= c <= 0x9FFF)
    print(f'{path}: total={len(ch)} cjk={cjk} missing={"".join(missing) if missing else "none"}')
