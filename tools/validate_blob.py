#!/usr/bin/env python3
"""Validate pop_sprites.bin by rendering sprites to PNG montages.
Reconstructs RGB from each chtab's own baked vga palette (values 0..63 -> <<2).
Pixel value 0 = transparent (shown as magenta checker); else color = value-base_row*16."""
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'PSPR', "bad magic"
    ver, ccount = struct.unpack_from('<HH', d, 4)
    chtabs = []
    off = 8
    for _ in range(ccount):
        dat = d[off:off+16].split(b'\0')[0].decode()
        resource, palbits, variant, nimg = struct.unpack_from('<HHHH', d, off+16)
        vga = list(struct.unpack_from('<48B', d, off+24))  # 16*rgb
        idir_off, rsvd = struct.unpack_from('<II', d, off+72)
        off += 80
        imgs = []
        for i in range(nimg):
            w, h, poff = struct.unpack_from('<HHI', d, idir_off + i*8)
            imgs.append((w, h, poff))
        chtabs.append(dict(dat=dat, resource=resource, palbits=palbits,
                           variant=variant, nimg=nimg, vga=vga, imgs=imgs))
    return d, chtabs

def base_row(palbits):
    for i in range(16):
        if palbits & (1 << i):
            return i * 16
    return 0

def write_png(path, w, h, rgb):
    # minimal PNG writer (RGB), no external deps
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y*w*3:(y+1)*w*3]
    def chunk(typ, data):
        c = struct.pack('>I', len(data)) + typ + data
        return c + struct.pack('>I', zlib.crc32(typ + data) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    png += chunk(b'IEND', b'')
    open(path, 'wb').write(png)

def render_montage(d, ct, indices, cols, out):
    base = base_row(ct['palbits'])
    vga = ct['vga']
    cell_w = max(ct['imgs'][i][0] for i in indices) + 2
    cell_h = max(ct['imgs'][i][1] for i in indices) + 2
    rows = (len(indices) + cols - 1) // cols
    W, H = cell_w * cols, cell_h * rows
    img = bytearray(W * H * 3)
    # background: dark gray
    for k in range(0, len(img), 3):
        img[k] = img[k+1] = img[k+2] = 40
    for n, idx in enumerate(indices):
        w, h, poff = ct['imgs'][idx]
        if poff == 0 or w == 0:
            continue
        cx = (n % cols) * cell_w + 1
        cy = (n // cols) * cell_h + 1
        px = d[poff:poff + w*h]
        for y in range(h):
            for x in range(w):
                v = px[y*w + x]
                if v == 0:
                    continue  # transparent
                ci = (v - base)
                if ci < 0 or ci > 15:
                    r, g, b = 255, 0, 255  # out-of-row => magenta (bug marker)
                else:
                    r = vga[ci*3+0] << 2
                    g = vga[ci*3+1] << 2
                    b = vga[ci*3+2] << 2
                o = ((cy+y)*W + (cx+x)) * 3
                img[o] = r; img[o+1] = g; img[o+2] = b
    write_png(out, W, H, img)
    print(f"wrote {out}  {W}x{H}  ({len(indices)} cells)")

def main():
    d, chtabs = load(sys.argv[1] if len(sys.argv) > 1 else 'data_gen/pop_sprites.bin')
    for ct in chtabs:
        print(f"{ct['dat']:14s} res{ct['resource']} var{ct['variant']} "
              f"bits0x{ct['palbits']:04x} nimg{ct['nimg']}")
    def find(dat, res, var=0):
        for ct in chtabs:
            if ct['dat'] == dat and ct['resource'] == res and ct['variant'] == var:
                return ct
        return None
    kid = find('KID.DAT', 400)
    render_montage(d, kid, list(range(0, min(48, kid['nimg']))), 12, '/tmp/kid.png')
    env = find('VDUNGEON.DAT', 200)
    nn = [i for i in range(env['nimg']) if env['imgs'][i][2] != 0][:48]
    render_montage(d, env, nn, 12, '/tmp/dungeon.png')
    guard = find('GUARD.DAT', 750, 0)
    render_montage(d, guard, list(range(0, min(34, guard['nimg']))), 12, '/tmp/guard.png')

if __name__ == '__main__':
    main()
