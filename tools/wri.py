#!/usr/bin/env python3
"""wri - read, dump, rewrite and compare Write (.WRI) documents.

The layout is the one written down in docs/wri-format.md, observed
black-box on the reference Windows 3.1 (documents saved by the original
Write under scripted input, probes WR*; ARCHITECTURE.md 3). No Windows
code is read.

usage:
  wri.py dump FILE            every structure, decoded
  wri.py text FILE            the text, paragraphs as lines
  wri.py rewrite IN OUT       parse and write back (structure round trip;
                              slack bytes are kept as read)
  wri.py cmp A B              compare two .WRI files; bytes Write never
                              defines (the unused middle of FKP pages,
                              which the original leaves as stale memory)
                              are ignored. Exit status 1 if they differ.
"""
import struct
import sys

PAGE = 128


def w(b, o):
    return struct.unpack_from('<H', b, o)[0]


def sw(b, o):
    return struct.unpack_from('<h', b, o)[0]


def dw(b, o):
    return struct.unpack_from('<I', b, o)[0]


class Doc:
    """A parsed .WRI file. Keeps every page so that a rewrite reproduces
    the input; the decoded views below are for dumping and checking."""

    def __init__(self, data):
        self.data = data
        b = data
        self.ident = w(b, 0)
        if self.ident not in (0xBE31, 0xBE32):
            raise ValueError('not a Write file (wIdent %04X)' % self.ident)
        self.fcMac = dw(b, 14)
        (self.pnPara, self.pnFntb, self.pnSep, self.pnSetb, self.pnPgtb,
         self.pnFfntb) = struct.unpack_from('<6H', b, 18)
        self.pnMac = w(b, 96)
        self.pnChar = (self.fcMac + PAGE - 1) // PAGE
        self.text = b[PAGE:self.fcMac]

    def page(self, pn):
        return self.data[pn * PAGE:(pn + 1) * PAGE]

    def fkps(self, first, last):
        """FKP pages first..last-1: (pn, fcFirst, [(fcLim, bfprop, fprop)])"""
        out = []
        for pn in range(first, last):
            p = self.page(pn)
            cfod = p[127]
            fods = []
            for i in range(cfod):
                fcl, bf = struct.unpack_from('<IH', p, 4 + 6 * i)
                pr = None if bf == 0xFFFF else p[4 + bf + 1:4 + bf + 1 + p[4 + bf]]
                fods.append((fcl, bf, pr))
            out.append((pn, dw(p, 0), fods))
        return out

    def chps(self):
        return self.fkps(self.pnChar, self.pnPara)

    def paps(self):
        return self.fkps(self.pnPara, self.pnFntb)

    def slack(self):
        """byte ranges (file offsets) the original Write leaves undefined
        (stale memory, WR2): the rest of the last text page after fcMac;
        in every FKP page the gap between the FOD array and the first
        FPROP; and each SEP, SETB, PGTB and FFNTB page after its
        structure."""
        r = [(self.fcMac, self.pnChar * PAGE)]
        for pn in range(self.pnChar, self.pnFntb):
            p = self.page(pn)
            cfod = p[127]
            lo = 4 + 6 * cfod
            hi = 127
            for i in range(cfod):
                bf = w(p, 4 + 6 * i + 4)
                if bf != 0xFFFF:
                    hi = min(hi, 4 + bf)
            r.append((pn * PAGE + lo, pn * PAGE + hi))
        if self.pnSep != self.pnSetb:
            r.append((self.pnSep * PAGE + 1 + self.page(self.pnSep)[0], self.pnSetb * PAGE))
        if self.pnSetb != self.pnPgtb:
            r.append((self.pnSetb * PAGE + 4 + 10 * w(self.page(self.pnSetb), 0), self.pnPgtb * PAGE))
        if self.pnPgtb != self.pnFfntb:
            r.append((self.pnPgtb * PAGE + 4 + 6 * w(self.page(self.pnPgtb), 0), self.pnFfntb * PAGE))
        if self.pnFfntb != self.pnMac:
            r.append((self.ffntb_end(), self.pnMac * PAGE))
        # OLE objects: header word 34 changes with every save (WR6); the
        # object number at 24 (dwObjNum, SDK ch. 8) is made from the clock
        # when an object is made (WR10)
        fc = PAGE
        for _, _, fods in self.paps():
            for fcl, _, pr in fods:
                if pr and len(pr) > 16 and pr[16] & 0x10 and fc + 36 <= self.fcMac \
                        and w(self.data, fc) == 0xE4:
                    r.append((fc + 34, fc + 36))
                    r.append((fc + 24, fc + 28))
                    r += self.static_stale(fc + 40, fcl)
                fc = fcl
        return r

    def static_stale(self, o, lim):
        """a static DIB object's stream (OLECLI's: version, format 3, class
        "DIB", HIMETRIC size, data size, the packed DIB) holds more data
        than the DIB needs: the last bytes (24 in WR16) are stale memory,
        different with every run. A static METAFILEPICT's data starts with
        the 16-bit METAFILEPICT, whose hMF (word 6) is a stale handle."""
        b = self.data
        try:
            if dw(b, o + 4) != 3:
                return []
            n = dw(b, o + 8)
            d = o + 12 + n + 12
            if b[o + 12:o + 12 + n] == b'METAFILEPICT\0' and d + 8 <= lim:
                return [(d + 6, d + 8)]
            if b[o + 12:o + 12 + n] != b'DIB\0':
                return []
            size = dw(b, d - 4)
            bits, planes, ncol, img = w(b, d + 14), w(b, d + 12), dw(b, d + 32), dw(b, d + 20)
            if not ncol and bits * planes <= 8:
                ncol = 1 << (bits * planes)
            if not img:
                img = ((dw(b, d + 4) * bits * planes + 31) // 32) * 4 * abs(dw(b, d + 8))
            end = d + dw(b, d) + 4 * ncol + img
            if end < d + size <= lim:
                return [(end, d + size)]
        except (IndexError, struct.error):
            pass
        return []

    def ffntb_end(self):
        """file offset just past the font table's terminating zero word"""
        pn = self.pnFfntb
        p = self.page(pn)
        n = w(p, 0)
        o = 2
        k = 0
        while True:
            cb = w(p, o) if o + 2 <= PAGE else 0xFFFF
            if cb == 0xFFFF:
                pn += 1
                p = self.page(pn)
                o = 0
                continue
            if cb == 0 or k == n:
                return pn * PAGE + o + 2 if cb == 0 else pn * PAGE + o
            k += 1
            o += 2 + cb

    def sep(self):
        if self.pnSep == self.pnSetb:
            return None
        p = self.page(self.pnSep)
        return p[1:1 + p[0]]

    def setb(self):
        if self.pnSetb == self.pnPgtb:
            return None
        p = self.page(self.pnSetb)
        n, nmax = w(p, 0), w(p, 2)
        return n, nmax, [struct.unpack_from('<HII', p, 4 + 10 * i) for i in range(n)]

    def pgtb(self):
        if self.pnPgtb == self.pnFfntb:
            return None
        b = self.data[self.pnPgtb * PAGE:self.pnFfntb * PAGE]
        n, x = w(b, 0), w(b, 2)
        return n, x, [struct.unpack_from('<HI', b, 4 + 6 * i) for i in range(n)]

    def ffntb(self):
        if self.pnFfntb == self.pnMac:
            return None
        fonts = []
        pn = self.pnFfntb
        p = self.page(pn)
        n = w(p, 0)
        o = 2
        while len(fonts) < n:
            cb = w(p, o)
            if cb == 0xFFFF:            # continued on the next page
                pn += 1
                p = self.page(pn)
                o = 0
                continue
            if cb == 0:
                break
            ffid = p[o + 2]
            name = p[o + 3:o + 2 + cb].split(b'\0')[0].decode('cp1252')
            fonts.append((ffid, name, cb))
            o += 2 + cb
        return n, fonts


def chp_desc(pr):
    if pr is None:
        return 'default'
    b = bytearray(pr) + bytearray(8)
    s = 'raw=%s b0=%02X' % (pr.hex(), b[0])
    ftc = (b[1] >> 2) | ((b[4] & 7) << 6)
    s += ' ftc=%d%s%s hps=%d' % (ftc, ' bold' if b[1] & 1 else '', ' italic' if b[1] & 2 else '',
                                 b[2] if len(pr) >= 3 else 24)
    if len(pr) >= 4:
        s += ' b3=%02X%s' % (b[3], ' underline' if b[3] & 1 else '')
    if len(pr) >= 6:
        s += ' hpsPos=%d' % struct.unpack('b', bytes(b[5:6]))[0]
    return s


def pap_desc(pr):
    if pr is None:
        return 'default'
    b = bytes(pr) + bytes(80)
    s = 'cch=%d b0=%02X jc=%d dxaRight=%d dxaLeft=%d dxaLeft1=%d dyaLine=%d rhc=%02X' % (
        len(pr), b[0], b[1], sw(b, 4), sw(b, 6), sw(b, 8), sw(b, 10), b[16])
    tabs = []
    for i in range((max(0, len(pr) - 22)) // 4):
        dxa, jct, ch = struct.unpack_from('<HBB', b, 22 + 4 * i)
        tabs.append('%d%s' % (dxa, 'd' if jct & 3 == 3 else ''))
    if tabs:
        s += ' tabs=' + ','.join(tabs)
    return s


def dump(d):
    print('wIdent %04X fcMac %d (text %d bytes) pnChar %d pnPara %d pnFntb %d pnSep %d '
          'pnSetb %d pnPgtb %d pnFfntb %d pnMac %d (file %d pages)' % (
              d.ident, d.fcMac, d.fcMac - PAGE, d.pnChar, d.pnPara, d.pnFntb, d.pnSep,
              d.pnSetb, d.pnPgtb, d.pnFfntb, d.pnMac, len(d.data) // PAGE))
    hdr = d.data[:PAGE]
    other = [i for i in range(PAGE) if hdr[i] and not (0 <= i < 6 or 14 <= i < 30 or 96 <= i < 98)]
    if other:
        print('  other header bytes:', ' '.join('%d=%02X' % (i, hdr[i]) for i in other))
    for name, fk, desc in (('CHP', d.chps(), chp_desc), ('PAP', d.paps(), pap_desc)):
        for pn, fc, fods in fk:
            print('%s page %d fcFirst %d cfod %d' % (name, pn, fc, len(fods)))
            for fcl, bf, pr in fods:
                print('   fcLim %d bfprop %s %s' % (fcl, 'FFFF' if bf == 0xFFFF else '%02X' % bf, desc(pr)))
    if d.pnFntb != d.pnSep:
        print('footnote table pages %d..%d' % (d.pnFntb, d.pnSep - 1))
    print('SEP', d.sep() and d.sep().hex())
    print('SETB', d.setb())
    print('PGTB', d.pgtb())
    print('FFNTB', d.ffntb())


def text(d):
    t = d.text.decode('cp1252', 'replace')
    sys.stdout.write(t.replace('\r\n', '\n'))


def cmp(a, b):
    da, db = Doc(a), Doc(b)
    mask = set()
    for lo, hi in da.slack() + db.slack():
        mask.update(range(lo, hi))
    diffs = [i for i in range(max(len(a), len(b)))
             if i not in mask and (i >= len(a) or i >= len(b) or a[i] != b[i])]
    return diffs


def build_fkps(fcFirst, runs, merge):
    """FKP pages for runs [(fcLim, fprop bytes or None)] as the original
    Write lays them out (observed, WR2/WR3): equal neighbours merged (CHP
    only), FODs from byte 4 up, FPROPs packed down from byte 127; a PAP
    FOD whose FPROP has the same bytes as the last FPROP stored in the page
    shares it (identical FPROPs further back are stored again, NETWORKS.WRI;
    a default FOD in between ends the sharing, WR6), each
    CHP FOD has its own (merge implies CHP); a new page
    when the next FOD (and its FPROP if new) no longer fits; the next
    page's fcFirst is the last fcLim before it."""
    if merge:
        m = []
        for fcl, pr in runs:
            if m and m[-1][1] == pr:
                m[-1] = (fcl, pr)
            else:
                m.append((fcl, pr))
        runs = m
    pages = []
    i = 0
    while i < len(runs):
        page = bytearray(PAGE)
        struct.pack_into('<I', page, 0, fcFirst)
        top = 127                       # FPROPs occupy [top, 127)
        last = lastbf = None
        n = 0
        while i < len(runs):
            fcl, pr = runs[i]
            if pr is None or (pr == last and not merge):
                need = 6
            else:
                need = 6 + 1 + len(pr)
            if 4 + 6 * n + need > top:
                break
            if pr is None:
                bf = 0xFFFF
                last = None             # a default FOD ends the sharing (WR6)
            elif pr == last and not merge:
                bf = lastbf
            else:
                top -= 1 + len(pr)
                page[top] = len(pr)
                page[top + 1:top + 1 + len(pr)] = pr
                bf = top - 4
                last, lastbf = pr, bf
            struct.pack_into('<IH', page, 4 + 6 * n, fcl, bf)
            n += 1
            fcFirst = fcl
            i += 1
        page[127] = n
        pages.append(page)
    return pages


def rebuild(d):
    """the file written again from its decoded content: header, text, FKPs
    rebuilt by build_fkps, the other tables copied"""
    chp = [(fcl, pr) for _, _, fods in d.chps() for fcl, _, pr in fods]
    pap = [(fcl, pr) for _, _, fods in d.paps() for fcl, _, pr in fods]
    out = bytearray(d.data[:d.pnChar * PAGE])
    out += b''.join(build_fkps(PAGE, chp, True))
    assert len(out) == d.pnPara * PAGE, 'CHP pages %d, file has %d' % (len(out) // PAGE - d.pnChar, d.pnPara - d.pnChar)
    out += b''.join(build_fkps(PAGE, pap, False))
    assert len(out) == d.pnFntb * PAGE, 'PAP pages differ'
    out += d.data[d.pnFntb * PAGE:]
    return bytes(out)


def main():
    cmd = sys.argv[1]
    if cmd == 'dump':
        dump(Doc(open(sys.argv[2], 'rb').read()))
    elif cmd == 'text':
        text(Doc(open(sys.argv[2], 'rb').read()))
    elif cmd == 'cmp':
        a, b = open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read()
        diffs = cmp(a, b)
        if len(a) != len(b):
            print('sizes differ: %d %d' % (len(a), len(b)))
        if diffs:
            print('%d bytes differ (outside FKP slack); first at %d (page %d +%d)' % (
                len(diffs), diffs[0], diffs[0] // PAGE, diffs[0] % PAGE))
            sys.exit(1)
        print('same (outside FKP slack)')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
