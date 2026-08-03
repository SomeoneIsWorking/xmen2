import struct, zlib, os, sys

def extract_wad(wad_path, outdir, want_substr=None):
    data = open(wad_path, 'rb').read()
    # find EOCD with most entries
    best = None
    i = 0
    while True:
        i = data.find(b'PK\x05\x06', i)
        if i < 0: break
        if i + 22 <= len(data):
            rec = data[i:i+22]
            tot = struct.unpack_from('<H', rec, 10)[0]
            cdsize = struct.unpack_from('<I', rec, 12)[0]
            cdoff = struct.unpack_from('<I', rec, 16)[0]
            if tot > 0 and cdoff + cdsize <= len(data):
                if best is None or tot > best[0]:
                    best = (tot, cdsize, cdoff)
        i += 4
    tot, cdsize, cdoff = best
    cd = data[cdoff:cdoff+cdsize]
    pos = 0
    extracted = 0
    while pos < len(cd):
        if cd[pos:pos+4] != b'PK\x01\x02': break
        method = struct.unpack_from('<H', cd, pos+10)[0]
        csize = struct.unpack_from('<I', cd, pos+20)[0]
        usize = struct.unpack_from('<I', cd, pos+24)[0]
        nlen = struct.unpack_from('<H', cd, pos+28)[0]
        elen = struct.unpack_from('<H', cd, pos+30)[0]
        clen = struct.unpack_from('<H', cd, pos+32)[0]
        loff = struct.unpack_from('<I', cd, pos+42)[0]
        name = cd[pos+46:pos+46+nlen].decode('latin1')
        pos += 46 + nlen + elen + clen
        if not name or name.endswith('/'):
            continue
        if want_substr and not any(w in name.lower() for w in want_substr):
            continue
        lh = data[loff:loff+30]
        nl, xl = struct.unpack_from('<HH', lh, 26)
        ds = loff + 30 + nl + xl
        comp = data[ds:ds+csize]
        try:
            raw = zlib.decompress(comp, -15) if method == 8 else comp
        except Exception:
            raw = comp
        path = os.path.join(outdir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(raw)
        extracted += 1
    return extracted

if __name__ == '__main__':
    want = sys.argv[3].split(',') if len(sys.argv) > 3 else None
    n = extract_wad(sys.argv[1], sys.argv[2], want)
    print("extracted", n)
