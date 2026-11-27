#!/usr/bin/env python3
"""ttautohint - w16's TrueType autohinter (ARCHITECTURE.md 6.9.3).

w16's own code, dedicated to the public domain under CC0 1.0 (LICENSE).
Written from the TrueType specification (Apple's TrueType Reference Manual,
Microsoft's TrueType instruction set); no other autohinter's or
rasterizer's source was read. Not related to the GPL tool of a similar
name: the bytecode it puts into fonts is w16's own (tools/tthint.py).

    ttautohint.py IN.TTF OUT.TTF [--keep-device-metrics]

Takes any TrueType font with glyf outlines and writes it again with
hints of its own in place of whatever hints it had:

  - blue zones per font from reference glyphs (Latin): baseline and its
    overshoot (bottoms of H, x / o, O), x-height (x, z / o), cap height
    (H, E / O), ascender (d, l, h / -), descender (p, q / g); a zone is
    left out when its glyphs are missing;
  - standard stem widths: every glyph's stems (an edge and the nearest
    edge facing it across the ink, in y and in x) are collected and the
    most frequent widths become control values;
  - per glyph (tools/tthint.py): blue-zone edges pinned (MIAP), stems
    linked with a minimum distance (font program functions: MIRP to the
    standard width, or MDRP), bars and serifs rounded and linked the same
    way, the rest interpolated (IUP), y first, then x;
  - prep: dropout control (SCANCTRL 511, SCANTYPE 1), top zones rounded
    up from 1/8 pixel, overshoots suppressed below the size where they
    reach a pixel; gasp: grid-fit and smoothing at every size.

Composite glyphs keep their components (which are hinted) and lose their
own instructions. The old fpgm, prep and cvt go; hdmx, LTSH and VDMX,
which record the old hinting's device metrics, go too unless
--keep-device-metrics. Only TrueType 1.0 instructions are emitted
(Windows 3.1's rasterizer); advance widths are not touched.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import w16ttf  # noqa: E402
import tthint  # noqa: E402


def _tables(b):
    return {t: b[o:o + n] for t, _, o, n in w16ttf._dir(b)}


def _strip_composite(d):
    """a composite glyph's bytes without its instructions"""
    p = 10
    while True:
        flags = struct.unpack_from('>H', d, p)[0]
        struct.pack_into('>H', d, p, flags & ~0x0100)
        p += 4 + (4 if flags & 1 else 2)
        if flags & 0x0008:
            p += 2
        elif flags & 0x0040:
            p += 4
        elif flags & 0x0080:
            p += 8
        if not flags & 0x0020:
            break
    return bytes(d[:p])


def zones_from(f, cmap):
    """blue zones (flat, overshoot, kind) from reference glyphs"""
    def box(chars):
        bs = []
        for ch in chars:
            gid = cmap.get(ord(ch))
            if gid and f['glyphs'][gid]['contours']:
                bs.append(f['glyphs'][gid]['bbox'])
        return bs

    def flat_round(flat_chars, round_chars, top):
        fb, rb = box(flat_chars), box(round_chars)
        if not fb:
            return None
        flat = sorted(b[3] if top else b[1] for b in fb)[len(fb) // 2]
        over = flat
        if rb:
            over = sorted(b[3] if top else b[1] for b in rb)[len(rb) // 2]
            if (over - flat) * (1 if top else -1) <= 0 or abs(over - flat) > 0.05 * f['head']['upem']:
                over = flat
        return flat, over
    zones = []
    for flat_c, round_c, top in (('Hx', 'oO', False), ('xzvw', 'oe', True), ('HEIT', 'OC', True),
                                 ('dlhkb', '', True), ('pq', 'g', False)):
        z = flat_round(flat_c, round_c, top)
        if z is None:
            continue
        if any(abs(z[0] - q[0]) <= 8 and q[2] == (1 if top else -1) for q in zones):
            continue
        zones.append((z[0], z[1], 1 if top else -1))
    return zones


def autohint(data, zones=None, keep_device=False):
    """the font's bytes with w16's hints; zones as tthint.Hinter takes
    them (None: found from reference glyphs)"""
    t = _tables(data)
    f = w16ttf.parse(data)
    upem = f['head']['upem']
    cmap = {}
    for (pid, eid, fmt), m in f['cmap'].items():
        if (pid, eid) in ((3, 1), (0, 3), (3, 10)):
            cmap.update(m)
    if zones is None:
        zones = zones_from(f, cmap)
    # stem limits from the font: twice the stem of l / I, else 1/8 em
    ref = [f['glyphs'][cmap[ord(c)]] for c in 'lI' if cmap.get(ord(c))]
    maxw = upem / 8
    if ref and ref[0]['contours']:
        b = ref[0]['bbox']
        maxw = max(maxw, (b[2] - b[0]) * 2.2)
    h = tthint.Hinter(upem, zones, maxw * 1.4, maxw)
    simple = [g['contours'] for g in f['glyphs'] if not g.get('composite')]
    h.learn(simple)
    # glyf and loca again
    loca_long = f['head']['locafmt'] == 1
    glyf = b''
    loca = []
    maxins = 0
    for i, g in enumerate(f['glyphs']):
        loca.append(len(glyf))
        raw = t[b'glyf'][f['loca'][i]:f['loca'][i + 1]]
        if not raw:
            continue
        if g.get('composite'):
            d = _strip_composite(bytearray(raw))
        else:
            gg = w16ttf.Glyph('x', 0)
            gg.contours = g['contours']          # all of them, as stored
            gg.instructions = h.glyph(g['contours'])
            maxins = max(maxins, len(gg.instructions))
            d = gg.encode()
            # keep the stored bounding box (it may differ in old fonts)
            d = raw[:10] + d[10:]
        glyf += d + b'\0' * ((-len(d)) % 4 if loca_long else len(d) % 2)
    loca.append(len(glyf))
    if not loca_long and max(loca) > 0x1FFFE:
        loca_long = True
    if loca_long:
        t[b'loca'] = struct.pack('>%dI' % len(loca), *loca)
    else:
        t[b'loca'] = struct.pack('>%dH' % len(loca), *[v // 2 for v in loca])
    head = bytearray(t[b'head'])
    struct.pack_into('>h', head, 50, 1 if loca_long else 0)
    t[b'head'] = bytes(head)
    t[b'glyf'] = glyf
    t[b'cvt '] = struct.pack('>%dh' % len(h.cvt), *h.cvt)
    t[b'fpgm'] = tthint.fpgm()
    t[b'prep'] = h.prep()
    t[b'gasp'] = struct.pack('>HHHH', 1, 1, 0xFFFF, 0x000F)
    m = bytearray(t[b'maxp'])
    if len(m) >= 32:
        struct.pack_into('>H', m, 14, max(2, struct.unpack_from('>H', m, 14)[0]))
        struct.pack_into(">H", m, 20, tthint.N_FUNCS)  # maxFunctionDefs
        struct.pack_into('>H', m, 22, 0)          # maxInstructionDefs
        struct.pack_into('>H', m, 24, 32)         # maxStackElements
        struct.pack_into('>H', m, 26, max(maxins, len(t[b'prep']), len(t[b'fpgm'])))
    t[b'maxp'] = bytes(m)
    if not keep_device:
        for tag in (b'hdmx', b'LTSH', b'VDMX'):
            t.pop(tag, None)
    return w16ttf.assemble(t), h


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    if len(a) != 2:
        print(__doc__)
        return 2
    data, h = autohint(open(a[0], 'rb').read(), keep_device='--keep-device-metrics' in sys.argv)
    open(a[1], 'wb').write(data)
    errs = [e for e in w16ttf.check(data) if 'composite' not in e]
    print('%s: zones %s, y stems %s, x stems %s%s' % (
        a[1], [z[:2] for z in h.zones], h.ystems, h.xstems,
        '' if not errs else '; problems: ' + '; '.join(errs[:5])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
