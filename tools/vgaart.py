#!/usr/bin/env python3
"""vgaart - build the w16 VGA display driver's graphics from their text
sources (drivers/vga/art/*.txt) into .BMP, .ICO and .CUR files, and draw a
contact sheet and a mock-up of them in use.

The art is w16's own (ARCHITECTURE.md 3: artwork is never copied). What it
must match in Microsoft's VGA.DRV are format facts only, read from the
resource headers by tools/vgainv.py: each resource's ID, size, colour
depth, header kind, palette order, cell layout and (cursors) hot spot.
drivers/vga/resources.md lists them.

Source format (one resource per file):

    # comment
    name OBM_CLOSE          the SDK name
    type bitmap             bitmap | icon | cursor
    id 32754                the resource ID
    size 36x18              width x height (every image of the file)
    bpp 4                   bitmap: 1 or 4
    header core             bitmap: core (BITMAPCOREHEADER) or info
    cells 18x18 2x1         optional: cell size, columns x rows
    hotspot 7,6             cursor: the hot spot
    image 16                icon: "image 16" (4 bpp), "image 2" (1 bpp),
                            optional "image 8" (else derived from image 16)
    pixels                  bitmap and cursor: the grid follows
    <one line per row, one character per pixel>

Pixel characters:
    #  black       -  white       +  silver (192)    =  gray (128)
    r  maroon      g  green       y  olive           b  navy
    m  purple      c  teal        R  red             G  lime
    Y  yellow      B  blue        M  fuchsia         C  cyan
    .  transparent (icons, cursors: the screen shows through)
    ~  inverted    (icons, cursors: the screen inverted)
A 1-bpp image may only use # and - (and . ~ in icons and cursors).

usage: vgaart.py build ARTDIR OUTDIR     .bmp/.ico/.cur per resource
       vgaart.py check ARTDIR            parse and validate only
       vgaart.py sheet ARTDIR OUT.png    contact sheet, 1x and 4x
       vgaart.py mockup ARTDIR OUT.png   the art in a 3.1-style screen
"""
import glob
import os
import struct
import sys

# the 16 colours in the order of the reference driver's DIB colour tables
# (vgainv.py: index 7 is gray, 8 silver)
DIBPAL = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
          (0, 128, 128), (128, 128, 128), (192, 192, 192), (255, 0, 0), (0, 255, 0),
          (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]
CHARS = '#rgyb' + 'mc=+R' + 'GYBMC-'     # index in DIBPAL
RGB = {ch: DIBPAL[i] for i, ch in enumerate(CHARS)}
# the 8-colour image of an icon: each colour to one of black, red, lime,
# yellow, blue, fuchsia, cyan, white
TO8 = {'#': '#', 'r': 'R', 'g': 'G', 'y': 'Y', 'b': 'B', 'm': 'M', 'c': 'C', '=': '#',
       '+': '-', 'R': 'R', 'G': 'G', 'Y': 'Y', 'B': 'B', 'M': 'M', 'C': 'C', '-': '-',
       '.': '.', '~': '~'}


class Art:
    def __init__(self, path):
        self.path = path
        self.name = self.type = None
        self.id = self.bpp = None
        self.header = 'info'
        self.cells = None
        self.hotspot = None
        self.purpose = ''
        self.images = {}            # key: 'pixels' or 16 / 8 / 2
        cur = None
        for n, line in enumerate(open(path, encoding='ascii'), 1):
            line = line.rstrip('\n')
            if cur is not None and line and ' ' not in line:
                self.images[cur].append(line)       # a row of pixels
                continue
            if not line.strip() or line.startswith('# '):
                if line.startswith('# ') and not self.purpose:
                    self.purpose = line[2:]
                continue
            k, _, v = line.partition(' ')
            if k == 'pixels':
                cur = 'pixels'
                self.images[cur] = []
            elif k == 'image':
                cur = int(v)
                self.images[cur] = []
            elif k == 'name':
                self.name = v
            elif k == 'type':
                self.type = v
            elif k == 'id':
                self.id = int(v)
            elif k == 'size':
                self.w, self.h = map(int, v.split('x'))
            elif k == 'bpp':
                self.bpp = int(v)
            elif k == 'header':
                self.header = v
            elif k == 'cells':
                a, b = v.split()
                self.cells = tuple(map(int, a.split('x'))) + tuple(map(int, b.split('x')))
            elif k == 'hotspot':
                self.hotspot = tuple(map(int, v.split(',')))
            else:
                raise SystemExit(f'{path}:{n}: unknown line {line!r}')
        self.validate()

    def fail(self, msg):
        raise SystemExit(f'{self.path}: {msg}')

    def validate(self):
        if self.type not in ('bitmap', 'icon', 'cursor'):
            self.fail('type must be bitmap, icon or cursor')
        if not self.name or self.id is None:
            self.fail('name and id needed')
        if self.type == 'icon':
            if 16 not in self.images and 2 not in self.images:
                self.fail('an icon needs image 16 or image 2')
            if 16 in self.images and 8 not in self.images:
                self.images[8] = [''.join(TO8[c] for c in row) for row in self.images[16]]
        elif 'pixels' not in self.images:
            self.fail('pixels missing')
        if self.type == 'cursor' and not self.hotspot:
            self.fail('a cursor needs a hotspot')
        if self.type == 'bitmap' and self.bpp not in (1, 4):
            self.fail('bpp must be 1 or 4')
        for key, rows in self.images.items():
            if len(rows) != self.h:
                self.fail(f'image {key}: {len(rows)} rows, expected {self.h}')
            mono = key == 2 or self.type == 'cursor' or (self.type == 'bitmap' and self.bpp == 1)
            ok = set('#-') if mono else set(CHARS)
            if self.type != 'bitmap':
                ok |= set('.~')
            for y, row in enumerate(rows):
                if len(row) != self.w:
                    self.fail(f'image {key} row {y}: {len(row)} pixels, expected {self.w}')
                bad = set(row) - ok
                if bad:
                    self.fail(f'image {key} row {y}: characters {"".join(sorted(bad))} not allowed')
        if self.cells:
            cw, ch, nx, ny = self.cells
            if cw * nx > self.w or ch * ny > self.h:
                self.fail('cells do not fit')
        if self.hotspot and not (0 <= self.hotspot[0] < self.w and 0 <= self.hotspot[1] < self.h):
            self.fail('hotspot outside the image')

    # ---- file images ----
    @staticmethod
    def dib_bits(rows, bpp, index):
        out = bytearray()
        for row in reversed(rows):              # bottom-up
            bits = bytearray()
            if bpp == 4:
                vals = [index(c) for c in row] + [0]
                for x in range(0, len(row), 2):
                    bits.append(vals[x] << 4 | vals[x + 1])
            else:
                acc = 0
                for x, c in enumerate(row):
                    acc = acc << 1 | index(c)
                    if x % 8 == 7:
                        bits.append(acc)
                        acc = 0
                if len(row) % 8:
                    bits.append(acc << (8 - len(row) % 8))
            while len(bits) % 4:
                bits.append(0)
            out += bits
        return bytes(out)

    def bmp(self):
        rows = self.images['pixels']
        pal = DIBPAL if self.bpp == 4 else [(0, 0, 0), (255, 255, 255)]
        bits = self.dib_bits(rows, self.bpp,
                             CHARS.index if self.bpp == 4 else lambda c: 1 if c == '-' else 0)
        if self.header == 'core':
            hdr = struct.pack('<IHHHH', 12, self.w, self.h, 1, self.bpp)
            hdr += b''.join(struct.pack('BBB', b, g, r) for r, g, b in pal)
        else:
            hdr = struct.pack('<IiiHHIIiiII', 40, self.w, self.h, 1, self.bpp, 0, len(bits),
                              0, 0, 0, 0)
            hdr += b''.join(struct.pack('BBBB', b, g, r, 0) for r, g, b in pal)
        fh = struct.pack('<2sIHHI', b'BM', 14 + len(hdr) + len(bits), 0, 0, 14 + len(hdr))
        return fh + hdr + bits

    def icon_image(self, key, bpp=None):
        rows = self.images[key]
        bpp = bpp or (1 if key == 2 else 4)
        pal = [(0, 0, 0), (255, 255, 255)] if bpp == 1 else DIBPAL

        def xor(c):
            if bpp == 1:
                return 1 if c in '-~' else 0
            return 15 if c == '~' else (0 if c == '.' else CHARS.index(c))
        x = self.dib_bits(rows, bpp, xor)
        a = self.dib_bits(rows, 1, lambda c: 1 if c in '.~' else 0)
        hdr = struct.pack('<IiiHHIIiiII', 40, self.w, self.h * 2, 1, bpp, 0, len(x) + len(a),
                          0, 0, 0, 0)
        hdr += b''.join(struct.pack('BBBB', b, g, r, 0) for r, g, b in pal)
        return hdr + x + a, bpp

    def ico(self):
        """an .ICO with the images in the reference group's order: 8
        colours, 2, 16"""
        keys = [k for k in (8, 2, 16) if k in self.images]
        imgs = [self.icon_image(k) for k in keys]
        head = struct.pack('<HHH', 0, 1, len(imgs))
        off = 6 + 16 * len(imgs)
        ents = b''
        for k, (img, bpp) in zip(keys, imgs):
            ents += struct.pack('<BBBBHHII', self.w & 255, self.h & 255, k, 0, 1, bpp,
                                len(img), off)
            off += len(img)
        return head + ents + b''.join(i for i, _ in imgs)

    def cur(self):
        """a .CUR: one 1-bpp image and the hot spot"""
        img, _ = self.icon_image('pixels', bpp=1)
        head = struct.pack('<HHH', 0, 2, 1)
        ent = struct.pack('<BBBBHHII', self.w, self.h, 0, 0, self.hotspot[0], self.hotspot[1],
                          len(img), 22)
        return head + ent + img

    def filename(self):
        return self.name.lower() + {'bitmap': '.bmp', 'icon': '.ico', 'cursor': '.cur'}[self.type]


def load(artdir):
    arts = [Art(p) for p in sorted(glob.glob(os.path.join(artdir, '*.txt')))]
    ids = {}
    for a in arts:
        k = (a.type, a.id)
        if k in ids:
            raise SystemExit(f'{a.path}: {a.type} {a.id} also in {ids[k]}')
        ids[k] = a.path
    return arts


def build(artdir, out):
    os.makedirs(out, exist_ok=True)
    for a in load(artdir):
        data = a.bmp() if a.type == 'bitmap' else a.cur() if a.type == 'cursor' else a.ico()
        with open(os.path.join(out, a.filename()), 'wb') as f:
            f.write(data)


# ---- pictures (PIL) ----

def image_of(a, key=None, bg=(0, 128, 128)):
    """an RGB picture of one image; transparent shows bg, inverted its inverse"""
    from PIL import Image
    rows = a.images[key if key is not None else ('pixels' if 'pixels' in a.images else
                                                  16 if 16 in a.images else 2)]
    im = Image.new('RGB', (a.w, a.h))
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            b = bg(x, y) if callable(bg) else bg
            im.putpixel((x, y), b if c == '.' else tuple(255 - v for v in b) if c == '~'
                        else RGB[c])
    return im


def font():
    from PIL import ImageFont
    for p in ('/usr/share/fonts/TTF/DejaVuSans.ttf',
              '/usr/share/fonts/dejavu/DejaVuSans.ttf',
              '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if os.path.exists(p):
            return ImageFont.truetype(p, 11), ImageFont.truetype(p, 15)
    f = ImageFont.load_default()
    return f, f


def checker(x, y):
    return (255, 255, 255) if (x // 4 + y // 4) % 2 else (200, 200, 200)


def sheet(artdir, out):
    from PIL import Image, ImageDraw
    arts = load(artdir)
    order = {'bitmap': 0, 'icon': 1, 'cursor': 2}
    arts.sort(key=lambda a: (order[a.type], a.id))
    small, big = font()
    tiles = []
    for a in arts:
        keys = [None] if a.type != 'icon' else [k for k in (16, 8, 2) if k in a.images]
        pics = []
        for k in keys:
            pics.append(image_of(a, k, checker if a.type != 'bitmap' else (0, 0, 0)))
        tiles.append((a, keys, pics))
    W = 1200
    pad = 12
    y = 50
    x = pad
    rowh = 0
    place = []
    for a, keys, pics in tiles:
        tw = max(160, sum(p.width * 5 + 14 for p in pics) + 10)
        th = max(p.height for p in pics) * 4 + 52
        if x + tw > W - pad:
            x = pad
            y += rowh + pad
            rowh = 0
        place.append((x, y, tw, th, a, keys, pics))
        x += tw + pad
        rowh = max(rowh, th)
    H = y + rowh + pad
    sh = Image.new('RGB', (W, H), (240, 240, 236))
    d = ImageDraw.Draw(sh)
    d.text((pad, 14), 'w16 VGA display driver graphics (drivers/vga/art): %d resources, '
           '1x and 4x' % len(arts), fill=(0, 0, 0), font=big)
    for x, y, tw, th, a, keys, pics in place:
        d.rectangle([x, y, x + tw - 1, y + th - 1], outline=(160, 160, 160), fill=(255, 255, 255))
        px = x + 6
        for k, p in zip(keys, pics):
            sh.paste(p, (px, y + 6))
            px += p.width + 6
        for k, p in zip(keys, pics):
            sh.paste(p.resize((p.width * 4, p.height * 4), Image.NEAREST), (px, y + 6))
            if a.cells:
                cw, ch, nx, ny = a.cells
                for i in range(1, nx):
                    d.line([px + i * cw * 4, y + 6, px + i * cw * 4, y + 6 + p.height * 4 - 1],
                           fill=(255, 0, 255))
                for j in range(1, ny):
                    d.line([px, y + 6 + j * ch * 4, px + p.width * 4 - 1, y + 6 + j * ch * 4],
                           fill=(255, 0, 255))
            if a.hotspot:
                hx, hy = a.hotspot
                d.rectangle([px + hx * 4, y + 6 + hy * 4, px + hx * 4 + 3, y + 6 + hy * 4 + 3],
                            outline=(255, 0, 0))
            px += p.width * 4 + 8
        ty = y + th - 40
        d.text((x + 6, ty), f'{a.name}  {a.id}', fill=(0, 0, 0), font=small)
        info = f'{a.w}x{a.h} '
        if a.type == 'bitmap':
            info += f'{a.bpp} bpp {a.header}'
            if a.cells:
                info += ' cells %dx%d %dx%d' % a.cells
        elif a.type == 'icon':
            info += 'images ' + '/'.join({16: '16c', 8: '8c', 2: 'mono'}[k] for k in keys)
        else:
            info += 'hot spot %d,%d' % a.hotspot
        d.text((x + 6, ty + 14), info, fill=(80, 80, 80), font=small)
    sh.save(out)


# ---- the mock-up: a Windows 3.1-style screen drawn with this art ----

SYS = {'desktop': (0, 128, 128), 'caption': (0, 0, 128), 'inactive': (255, 255, 255),
       'frame': (0, 0, 0), 'border': (192, 192, 192), 'window': (255, 255, 255),
       'face': (192, 192, 192), 'shadow': (128, 128, 128), 'menu': (255, 255, 255),
       'scroll': (192, 192, 192), 'hilite': (0, 0, 128), 'text': (0, 0, 0)}


class Screen:
    def __init__(self, arts, w, h):
        from PIL import Image, ImageDraw
        self.by = {a.name: a for a in arts}
        self.im = Image.new('RGB', (w, h), SYS['desktop'])
        self.d = ImageDraw.Draw(self.im)
        self.small, self.big = font()

    def fill(self, x, y, w, h, c):
        if w > 0 and h > 0:
            self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    def frame(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)

    def blt(self, name, x, y, sx=0, sy=0, cw=None, ch=None, fg=(0, 0, 0), bg=(255, 255, 255),
            key=None):
        """a bitmap as USER blits it: mono 0 bits in the text colour, 1 in
        the background colour; icons and cursors over what is there"""
        a = self.by[name]
        rows = a.images[key if key is not None else ('pixels' if 'pixels' in a.images else 16)]
        cw = cw or a.w
        ch = ch or a.h
        mono = a.type == 'cursor' or (a.type == 'bitmap' and a.bpp == 1) or key == 2
        for j in range(ch):
            for i in range(cw):
                c = rows[sy + j][sx + i]
                p = (x + i, y + j)
                if not (0 <= p[0] < self.im.width and 0 <= p[1] < self.im.height):
                    continue
                if c == '.':
                    continue
                if c == '~':
                    self.im.putpixel(p, tuple(255 - v for v in self.im.getpixel(p)))
                elif mono and a.type == 'bitmap':
                    self.im.putpixel(p, fg if c == '#' else bg)
                else:
                    self.im.putpixel(p, RGB[c])

    def text(self, x, y, s, c=(0, 0, 0), f=None):
        self.d.text((x, y), s, fill=c, font=f or self.small)

    def bevel(self, x, y, w, h, pressed=False):
        self.fill(x, y, w, h, SYS['face'])
        if pressed:
            self.fill(x, y, w, 1, SYS['shadow'])
            self.fill(x, y, 1, h, SYS['shadow'])
        else:
            self.fill(x, y, w - 1, 1, (255, 255, 255))
            self.fill(x, y, 1, h - 1, (255, 255, 255))
            self.fill(x + w - 1, y, 1, h, SYS['shadow'])
            self.fill(x + w - 2, y + 1, 1, h - 1, SYS['shadow'])
            self.fill(x, y + h - 1, w, 1, SYS['shadow'])
            self.fill(x + 1, y + h - 2, w - 1, 1, SYS['shadow'])

    def button(self, x, y, w, h, label, default=False):
        corners = [(x, y), (x + w - 1, y), (x, y + h - 1), (x + w - 1, y + h - 1)]
        under = [self.im.getpixel(c) for c in corners]
        self.fill(x, y, w, h, SYS['face'])
        self.frame(x, y, w, h, (0, 0, 0))
        k = 2 if default else 1
        if default:
            self.frame(x + 1, y + 1, w - 2, h - 2, (0, 0, 0))
        self.bevel(x + k, y + k, w - 2 * k, h - 2 * k)
        # rounded corners: the four corner pixels of the outline left out
        for c, u in zip(corners, under):
            self.im.putpixel(c, u)
        tw = self.d.textlength(label, font=self.small)
        self.text(x + (w - tw) // 2, y + (h - 13) // 2, label)

    def window(self, x, y, w, h, title, active=True, sizable=True, minmax=True, child=False,
               maximized=False, dialog=False):
        """a frame as w16's USER draws it (user/defwnd.c): caption 20 rows
        with its black lines, system box, caption buttons, frame 4"""
        f = 4 if sizable else (4 if dialog else 1)
        self.fill(x, y, w, h, SYS['border'] if sizable else SYS['window'])
        self.frame(x, y, w, h, (0, 0, 0))
        if sizable:
            self.frame(x + f - 1, y + f - 1, w - 2 * f + 2, h - 2 * f + 2, (0, 0, 0))
            for k in (22,):        # the notches where the caption buttons meet the frame
                for cx in (x + k, x + w - 1 - k):
                    self.fill(cx, y, 1, f, (0, 0, 0))
                    self.fill(cx, y + h - f, 1, f, (0, 0, 0))
                for cy in (y + k, y + h - 1 - k):
                    self.fill(x, cy, f, 1, (0, 0, 0))
                    self.fill(x + w - f, cy, f, 1, (0, 0, 0))
        if dialog:
            self.fill(x + 1, y + 1, w - 2, f - 2, SYS['caption'])
            self.fill(x + 1, y + 1, f - 2, h - 2, SYS['caption'])
            self.fill(x + w - f + 1, y + 1, f - 2, h - 2, SYS['caption'])
            self.fill(x + 1, y + h - f + 1, w - 2, f - 2, SYS['caption'])
            self.frame(x + f - 1, y + f - 1, w - 2 * f + 2, h - 2 * f + 2, (0, 0, 0))
        cx, cy, cw = x + f - 1, y + f - 1, w - 2 * f + 2
        self.fill(cx, cy, cw, 20, SYS['caption'] if active else SYS['inactive'])
        self.frame(cx, cy, cw, 20, (0, 0, 0))
        tl = cx + 1
        self.blt('OBM_CLOSE', tl, cy + 1, sx=18 if child else 0, cw=18)
        self.fill(tl + 18, cy, 1, 20, (0, 0, 0))
        tl += 19
        tr = cx + cw - 1
        if minmax:
            tr -= 19
            self.blt('OBM_RESTORE' if maximized else 'OBM_ZOOM', tr, cy + 1)
            tr -= 19
            self.blt('OBM_REDUCE', tr, cy + 1)
        tw = self.d.textlength(title, font=self.small)
        self.text(tl + (tr - tl - tw) // 2, cy + 4, title,
                  (255, 255, 255) if active else (0, 0, 0))
        return cx + 1, cy + 20, cw - 2, y + h - f - (cy + 20) + 1

    def menubar(self, x, y, w, items, open_at=None):
        self.fill(x, y, w, 18, SYS['menu'])
        self.fill(x, y + 18, w, 1, (0, 0, 0))
        px = x
        pos = []
        for i, s in enumerate(items):
            tw = int(self.d.textlength(s, font=self.small)) + 16
            if i == open_at:
                self.fill(px, y, tw, 18, SYS['hilite'])
            self.text(px + 8, y + 2, s, (255, 255, 255) if i == open_at else (0, 0, 0))
            pos.append(px)
            px += tw
        return pos

    def vscroll(self, x, y, h, pos=0.3, pressed=None, disabled=False):
        self.fill(x, y, 17, h, SYS['scroll'])
        self.frame(x, y, 17, h, (0, 0, 0))
        g = 'I' if disabled else ''
        self.blt('OBM_UPARROW' + ('D' if pressed == 'up' else g), x, y)
        self.blt('OBM_DNARROW' + ('D' if pressed == 'dn' else g), x, y + h - 17)
        if not disabled:
            t = y + 16 + int((h - 2 * 16 - 17) * pos)
            self.fill(x, t, 17, 17, (0, 0, 0))
            self.bevel(x + 1, t + 1, 15, 15)

    def hscroll(self, x, y, w, pos=0.0, pressed=None, disabled=False):
        self.fill(x, y, w, 17, SYS['scroll'])
        self.frame(x, y, w, 17, (0, 0, 0))
        g = 'I' if disabled else ''
        self.blt('OBM_LFARROW' + ('D' if pressed == 'lf' else g), x, y)
        self.blt('OBM_RGARROW' + ('D' if pressed == 'rg' else g), x + w - 17, y)
        if not disabled:
            t = x + 16 + int((w - 2 * 16 - 17) * pos)
            self.fill(t, y, 17, 17, (0, 0, 0))
            self.bevel(t + 1, y + 1, 15, 15)

    def checkbox(self, x, y, label, row, col):
        self.blt('OBM_CHECKBOXES', x, y, sx=col * 14, sy=row * 13, cw=13, ch=13)
        self.text(x + 18, y, label)

    def icon(self, name, x, y, key=16):
        self.blt(name, x, y, key=key)

    def cursor(self, name, x, y):
        """the hot spot at x, y"""
        a = self.by[name]
        self.blt(name, x - a.hotspot[0], y - a.hotspot[1])


def mockup(artdir, out):
    from PIL import Image
    arts = load(artdir)
    s = Screen(arts, 1000, 700)
    sm, big = s.small, s.big
    # -- a sizable application window with a menu, scroll bars, size box
    x, y, w, h = s.window(10, 10, 470, 330, 'w16 Write - SAMPLE.WRI')
    pos = s.menubar(x, y, w, ['File', 'Edit', 'View', 'Options', 'Help'], open_at=2)
    s.fill(x, y + 19, w, h - 19, SYS['window'])
    for i, line in enumerate(['The quick brown fox jumps over', 'the lazy dog. These are the w16',
                              'display driver graphics, drawn', 'from scratch.']):
        s.text(x + 8, y + 26 + 16 * i, line)
    s.vscroll(x + w - 17, y + 19, h - 19 - 16, pos=0.35, pressed='dn')
    s.hscroll(x, y + h - 17, w - 16, pos=0.1)
    s.fill(x + w - 17, y + h - 17, 17, 17, SYS['face'])
    s.blt('OBM_BTSIZE', x + w - 16, y + h - 16, fg=(0, 0, 0), bg=SYS['face'])
    # the open View menu: check marks and a cascading item
    mx, my, mw = pos[2], y + 19, 170
    items = [('Ruler', True), ('Status Bar', True), ('Word Wrap', False), ('-', None),
             ('Zoom', 'cascade'), ('Font Size', 'cascade')]
    mh = 4 + sum(8 if t == '-' else 17 for t, _ in items)
    s.fill(mx, my, mw, mh, SYS['menu'])
    s.frame(mx, my, mw, mh, (0, 0, 0))
    s.fill(mx + mw, my + 2, 2, mh, (0, 0, 0))
    s.fill(mx + 2, my + mh, mw, 2, (0, 0, 0))
    yy = my + 2
    for i, (t, k) in enumerate(items):
        if t == '-':
            s.fill(mx + 1, yy + 3, mw - 2, 1, (0, 0, 0))
            yy += 8
            continue
        hil = i == 4
        bg = SYS['hilite'] if hil else SYS['menu']
        fg = (255, 255, 255) if hil else (0, 0, 0)
        s.fill(mx + 1, yy, mw - 2, 17, bg)
        if k is True:
            s.blt('OBM_CHECK', mx + 2, yy + 1, fg=fg, bg=bg)
        if k == 'cascade':
            s.blt('OBM_MNARROW', mx + mw - 14, yy + 3, fg=fg, bg=bg)
        s.text(mx + 18, yy + 2, t, fg)
        yy += 17
    s.cursor('OCR_NORMAL', mx + 60, yy - 20)

    # -- a dialog with every check box, radio button and 3-state cell
    x, y, w, h = s.window(500, 10, 490, 330, 'Options', sizable=False, minmax=False, dialog=True)
    s.fill(x, y, w, h, SYS['window'])
    labels = ['off', 'on', 'pushed', 'pushed, on']
    rowl = ['Check box', 'Radio button', 'Three-state']
    for r in range(3):
        s.text(x + 12, y + 14 + r * 26, rowl[r] + ':')
        for c in range(4):
            s.checkbox(x + 100 + c * 92, y + 14 + r * 26, labels[c] if r != 2 or c % 2 == 0
                       else labels[c].replace('on', 'grayed'), r, c)
    # a combo box with its arrow
    cy = y + 104
    s.text(x + 12, cy + 3, 'Font:')
    s.fill(x + 100, cy, 160, 22, SYS['window'])
    s.frame(x + 100, cy, 160, 22, (0, 0, 0))
    s.text(x + 104, cy + 4, 'MS Sans Serif')
    bx = x + 100 + 160 - 17
    s.frame(bx, cy, 17, 22, (0, 0, 0))
    s.bevel(bx + 1, cy + 1, 15, 20)
    s.blt('OBM_COMBO', bx + 4, cy + 6, bg=SYS['face'])
    # a list box with scroll bars: one disabled, one with a pressed arrow
    s.text(x + 12, cy + 40, 'Disabled:')
    s.fill(x + 100, cy + 34, 160, 100, SYS['window'])
    s.frame(x + 100, cy + 34, 160, 100, (0, 0, 0))
    s.vscroll(x + 100 + 160 - 17, cy + 34, 100, disabled=True)
    s.hscroll(x + 280, cy + 34, 180, pos=0.5, pressed='lf')
    s.hscroll(x + 280, cy + 60, 180, disabled=True)
    s.vscroll(x + 300, cy + 86, 50, pressed='up')
    s.vscroll(x + 330, cy + 86, 50, disabled=True)
    s.button(x + 160, h + y - 40, 80, 26, 'OK', default=True)
    s.button(x + 260, h + y - 40, 80, 26, 'Cancel')
    s.cursor('OCR_IBEAM', x + 180, cy + 12)

    # -- four message boxes, one per icon
    msgs = [('OIC_HAND', 'Stop', 'Cannot open the file.'),
            ('OIC_QUES', 'Question', 'Save changes to SAMPLE.WRI?'),
            ('OIC_BANG', 'Warning', 'The disk is almost full.'),
            ('OIC_NOTE', 'Information', 'Search text not found.')]
    for i, (ic, t, m) in enumerate(msgs):
        bx, by = 10 + i * 248, 352
        x, y, w, h = s.window(bx, by, 238, 120, t, sizable=False, minmax=False, dialog=True)
        s.fill(x, y, w, h, SYS['window'])
        s.icon(ic, x + 14, y + 14)
        s.text(x + 60, y + 24, m)
        s.button(x + w // 2 - 36, y + h - 34, 72, 24, 'OK', default=True)

    # -- a maximised-style child (restore button, child system box), an
    # inactive window, and the cursors on the desktop
    x, y, w, h = s.window(10, 484, 300, 120, 'Child window', active=False, child=True,
                          maximized=True)
    s.fill(x, y, w, h, SYS['window'])
    s.text(x + 8, y + 8, 'MDI child: short bar in the system box,')
    s.text(x + 8, y + 24, 'Restore button (window maximised).')
    s.text(x + 8, y + 52, 'Caption buttons pressed:')
    s.blt('OBM_REDUCED', x + 160, y + 46)
    s.blt('OBM_ZOOMD', x + 180, y + 46)
    s.blt('OBM_RESTORED', x + 200, y + 46)
    s.blt('OBM_SIZE', x + 240, y + 48)
    s.text(x + 8, y + 80, 'Desktop icon (OIC_SAMPLE, mono 64x64):')
    s.icon('OIC_SAMPLE', 330, 500, key=2)
    s.text(320, 568, 'Application', (255, 255, 255))
    curs = ['OCR_NORMAL', 'OCR_IBEAM', 'OCR_WAIT', 'OCR_CROSS', 'OCR_UP', 'OCR_SIZE',
            'OCR_ICON', 'OCR_SIZENWSE', 'OCR_SIZENESW', 'OCR_SIZEWE', 'OCR_SIZENS']
    for i, c in enumerate(curs):
        cx, cy = 430 + (i % 6) * 90, 520 + (i // 6) * 80
        s.fill(cx - 30, cy - 22, 60, 50, (255, 255, 255) if i % 2 else SYS['desktop'])
        s.cursor(c, cx, cy)
        s.text(cx - 30, cy + 30, c[4:], (255, 255, 255))
    s.im.save(out)
    big_out = os.path.splitext(out)[0] + '-2x.png'
    s.im.resize((s.im.width * 2, s.im.height * 2), Image.NEAREST).save(big_out)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'build':
        build(sys.argv[2], sys.argv[3])
    elif cmd == 'check':
        for a in load(sys.argv[2]):
            print(f'{a.type:6} {a.id:5} {a.name:16} {a.w}x{a.h} ok')
    elif cmd == 'sheet':
        sheet(sys.argv[2], sys.argv[3])
    elif cmd == 'mockup':
        mockup(sys.argv[2], sys.argv[3])
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
