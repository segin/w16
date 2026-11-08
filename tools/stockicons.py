"""stockicons - the stock icon set of w16's PROGMAN.EXE (original art).

Program Manager offers icons by index (ExtractIcon "PROGMAN.EXE", n);
group files and installers refer to them by that index (Windows 3.1's
Setup gives MS-DOS Prompt PROGMAN.EXE icon 9). The original has 46 such
icons; this set keeps their count and the subject of each index (observed
with tests/apps/ticons.c), drawn anew. The original's Microsoft logos
(8, 9) become the w16 logo and a DOS prompt.

Each function draws on a fresh 32x32 Icon (tools/mkicons.py).
"""
from mkicons import (Icon, BLACK, MAROON, GREEN, OLIVE, NAVY, PURPLE, TEAL, SILVER,
                     GRAY, RED, LIME, YELLOW, BLUE, FUCHSIA, CYAN, WHITE)


def slabs(i, x=5, y=10):
    """the w16 mark: three stacked slabs"""
    i.slab(x, y + 8, 17, WHITE, SILVER, GRAY)
    i.slab(x, y + 4, 17, TEAL, CYAN, NAVY)
    i.slab(x, y, 17, BLUE, CYAN, NAVY)


def win(i, body=WHITE, title=NAVY):
    i.window(1, 3, 30, 28, title, body)


def i00(i):  # Program Manager: a window holding the w16 mark
    i.window(1, 2, 30, 29, NAVY, SILVER)
    slabs(i)


def i01(i):  # MS-DOS session: a window with a prompt
    win(i, BLACK)
    i.text(4, 13, 'C:\\>', SILVER)
    i.rect(20, 17, 22, 17, WHITE)


def i02(i):  # application window: lines of text
    win(i)
    for y in (12, 15, 18, 21, 24):
        i.rect(5, y, 26 if y != 24 else 17, y, BLUE)


def i03(i):  # spreadsheet window: a grid
    win(i)
    for y in range(10, 28, 4):
        i.rect(2, y, 29, y, GRAY)
    for x in range(8, 30, 7):
        i.rect(x, 9, x, 27, GRAY)
    i.rect(2, 9, 29, 9, SILVER)
    i.rect(10, 15, 13, 17, GREEN)
    i.rect(17, 19, 20, 21, RED)


def i04(i):  # document window: heading and text
    win(i)
    i.rect(5, 11, 20, 12, RED)
    for y in (16, 19, 22, 25):
        i.rect(5, y, 26 if y != 25 else 14, y, BLACK)


def i05(i):  # communications window: a handset
    win(i)
    i.poly([(7, 22), (11, 13), (15, 13), (13, 17), (19, 17), (17, 13), (21, 13), (25, 22)],
           YELLOW, BLACK)
    i.rect(12, 20, 20, 23, OLIVE)


def i06(i):  # empty window
    win(i)


def i07(i):  # program group: a window of small programs
    win(i)
    for n, (fill, edge) in enumerate(((BLUE, NAVY), (CYAN, TEAL), (SILVER, GRAY))):
        x = 5 + n * 8
        i.rect(x, 12, x + 5, 17, fill)
        i.frame(x, 12, x + 5, 17, edge)
        i.rect(x, 20, x + 5, 20, GRAY)


def i08(i):  # the w16 logo
    slabs(i, 4, 4)
    i.text(8, 25, 'W16', NAVY)


def i09(i):  # a DOS prompt
    i.rect(2, 4, 29, 27, BLACK)
    i.frame(2, 4, 29, 27, GRAY)
    i.text(5, 9, 'C:\\>', WHITE)
    i.rect(21, 13, 23, 13, LIME)
    i.rect(5, 18, 26, 18, GRAY)
    i.rect(5, 21, 18, 21, GRAY)


def i10(i):  # drawing: pencils in a cup
    for x, c in ((10, RED), (15, BLUE), (20, LIME)):
        i.rect(x, 4, x + 2, 17, c)
        i.frame(x, 4, x + 2, 17, BLACK)
        i.poly([(x, 4), (x + 1, 1), (x + 2, 4)], OLIVE)
    i.poly([(7, 15), (25, 15), (23, 29), (9, 29)], TEAL, BLACK)
    i.rect(10, 18, 22, 18, CYAN)


def i11(i):  # 3-D bar chart
    i.line(4, 28, 28, 28, BLACK)
    i.line(4, 4, 4, 28, BLACK)
    for x, h, f, s in ((7, 10, RED, MAROON), (14, 18, LIME, GREEN), (21, 14, BLUE, NAVY)):
        i.rect(x, 27 - h, x + 4, 27, f)
        i.poly([(x + 5, 27 - h), (x + 7, 25 - h), (x + 7, 25), (x + 5, 27)], s)
        i.poly([(x, 27 - h), (x + 2, 25 - h), (x + 7, 25 - h), (x + 5, 27 - h)], s)
        i.frame(x, 27 - h, x + 4, 27, BLACK)


def i12(i):  # camera
    i.rect(3, 10, 28, 26, GRAY)
    i.frame(3, 10, 28, 26, BLACK)
    i.rect(10, 7, 18, 10, GRAY)
    i.frame(10, 7, 18, 10, BLACK)
    i.disc(16, 18, 6, BLACK)
    i.disc(16, 18, 4, NAVY)
    i.disc(14, 16, 1, CYAN)
    i.rect(23, 12, 26, 14, WHITE)


def i13(i):  # disc
    i.disc(16, 16, 13, SILVER)
    i.ring(16, 16, 13, GRAY)
    for r, c in ((10, CYAN), (8, FUCHSIA), (6, YELLOW)):
        for k in range(-3, 4):
            i.set(16 + r * 7 // 10 + k // 2, 16 - r * 7 // 10 + k // 2, c)
    i.disc(16, 16, 3, WHITE)
    i.ring(16, 16, 3, GRAY)
    i.set(16, 16, CLEAR_)


def i14(i):  # report: a page with a chart
    i.page(5, 2, 26, 29)
    i.rect(8, 6, 18, 7, NAVY)
    i.line(8, 25, 23, 25, BLACK)
    for x, h, c in ((9, 6, BLUE), (13, 10, RED), (17, 4, LIME), (21, 8, YELLOW)):
        i.rect(x, 25 - h, x + 2, 24, c)
    for y in (11, 13):
        i.rect(8, y, 23, y, GRAY)


def i15(i):  # fax: a machine sending a page
    i.page(9, 1, 23, 16)
    i.rect(11, 5, 21, 5, BLACK)
    i.rect(11, 8, 19, 8, BLACK)
    i.poly([(2, 15), (29, 15), (29, 28), (2, 28)], SILVER, BLACK)
    i.rect(5, 15, 26, 17, BLACK)
    i.rect(5, 21, 14, 25, TEAL)
    for x in (18, 21, 24):
        for y in (21, 24):
            i.rect(x, y, x + 1, y + 1, GRAY)


def i16(i):  # drafting: set square and ruler
    i.poly([(4, 4), (4, 26), (26, 26)], YELLOW, BLACK)
    i.poly([(8, 13), (8, 22), (17, 22)], CLEAR_)
    i.poly([(14, 2), (30, 18), (27, 21), (11, 5)], WHITE, BLACK)
    for k in range(3, 16, 3):
        i.line(13 + k, 4 + k, 14 + k, 3 + k, BLACK)


def i17(i):  # papers: a stack of pages
    for d in (6, 3, 0):
        i.page(4 + d, 2 + d, 21 + d, 23 + d)
    for y in (12, 15, 18, 21):
        i.rect(7, y, 19, y, BLUE)


def i18(i):  # film: a strip with frames
    i.rect(2, 7, 29, 24, BLACK)
    for x in range(3, 29, 4):
        i.rect(x, 8, x + 1, 9, WHITE)
        i.rect(x, 22, x + 1, 23, WHITE)
    for x, c in ((4, CYAN), (13, YELLOW), (22, FUCHSIA)):
        i.rect(x, 11, x + 6, 20, c)


def i19(i):  # line chart
    i.rect(3, 3, 28, 28, WHITE)
    i.frame(3, 3, 28, 28, BLACK)
    for y in range(8, 28, 5):
        i.rect(4, y, 27, y, SILVER)
    pts = [(5, 24), (10, 17), (14, 20), (19, 9), (23, 13), (26, 6)]
    for a, b in zip(pts, pts[1:]):
        i.line(a[0], a[1], b[0], b[1], RED)
        i.line(a[0], a[1] + 1, b[0], b[1] + 1, RED)


def i20(i):  # handshake: two hands meeting
    i.rect(1, 13, 8, 21, NAVY)
    i.rect(23, 13, 30, 21, GRAY)
    i.poly([(8, 13), (16, 11), (23, 13), (23, 21), (16, 23), (8, 21)], YELLOW, BLACK)
    for x in (13, 16, 19):
        i.line(x, 13, x + 2, 21, OLIVE)


def i21(i):  # tools: a folding pocket knife
    i.poly([(3, 20), (22, 9), (26, 14), (7, 25)], RED, BLACK)
    i.poly([(20, 10), (29, 2), (30, 4), (23, 12)], SILVER, BLACK)
    i.poly([(5, 21), (2, 28), (4, 29), (8, 23)], SILVER, BLACK)
    i.rect(13, 16, 15, 18, WHITE)


def i22(i):  # mail: envelopes
    for d, c in ((6, SILVER), (0, WHITE)):
        i.rect(3 + d, 6 + d, 24 + d, 20 + d, c)
        i.frame(3 + d, 6 + d, 24 + d, 20 + d, BLACK)
        i.line(3 + d, 6 + d, 13 + d, 14 + d, BLACK)
        i.line(24 + d, 6 + d, 14 + d, 14 + d, BLACK)
    i.rect(21, 9, 23, 11, RED)


def i23(i):  # picture in a frame
    i.rect(2, 2, 29, 29, OLIVE)
    i.frame(2, 2, 29, 29, BLACK)
    i.rect(6, 6, 25, 25, CYAN)
    i.poly([(6, 25), (13, 15), (19, 21), (22, 18), (25, 22), (25, 25)], GREEN)
    i.disc(20, 10, 2, YELLOW)
    i.frame(6, 6, 25, 25, BLACK)


def i24(i):  # newspaper
    i.rect(3, 4, 28, 27, WHITE)
    i.frame(3, 4, 28, 27, BLACK)
    i.rect(6, 7, 25, 9, BLACK)
    i.rect(6, 12, 14, 19, GRAY)
    for y in range(12, 26, 2):
        i.rect(17, y, 25, y, GRAY)
    for y in (22, 24):
        i.rect(6, y, 14, y, GRAY)


def i25(i):  # telephone
    i.poly([(6, 19), (9, 13), (22, 13), (25, 19), (25, 27), (6, 27)], YELLOW, BLACK)
    i.poly([(3, 9), (28, 9), (28, 12), (22, 12), (21, 10), (10, 10), (9, 12), (3, 12)], OLIVE, BLACK)
    i.disc(15, 20, 4, WHITE)
    i.ring(15, 20, 4, BLACK)
    i.disc(15, 20, 1, BLACK)


def i26(i):  # a blank page
    i.page(6, 2, 25, 29, 6)


def i27(i):  # accounts: a ledger and calculator
    i.page(3, 2, 20, 27)
    for y in range(7, 25, 3):
        i.rect(5, y, 17, y, GREEN)
    i.rect(14, 12, 29, 29, GRAY)
    i.frame(14, 12, 29, 29, BLACK)
    i.rect(16, 14, 27, 17, LIME)
    for x in (16, 20, 24):
        for y in (20, 23, 26):
            i.rect(x, y, x + 2, y + 1, WHITE)


def i28(i):  # memo: a note held by a pin
    i.poly([(4, 8), (26, 5), (28, 27), (6, 29)], YELLOW, BLACK)
    for y in (14, 18, 22):
        i.line(8, y + 1, 24, y - 1, OLIVE)
    i.disc(16, 6, 3, RED)
    i.line(16, 8, 16, 11, BLACK)


def i29(i):  # cassette tape
    i.rect(2, 7, 29, 25, GRAY)
    i.frame(2, 7, 29, 25, BLACK)
    i.rect(6, 10, 25, 18, WHITE)
    i.disc(11, 14, 3, BLACK)
    i.disc(20, 14, 3, BLACK)
    i.disc(11, 14, 1, WHITE)
    i.disc(20, 14, 1, WHITE)
    i.poly([(8, 25), (10, 21), (21, 21), (23, 25)], SILVER, BLACK)


def i30(i):  # typewriter
    i.page(9, 2, 22, 13)
    i.rect(2, 12, 29, 15, BLACK)
    i.poly([(3, 16), (28, 16), (30, 28), (1, 28)], SILVER, BLACK)
    for x in range(5, 27, 4):
        i.rect(x, 19, x + 2, 20, BLACK)
        i.rect(x + 1, 23, x + 3, 24, BLACK)


def i31(i):  # briefcase
    i.rect(2, 10, 29, 27, MAROON)
    i.frame(2, 10, 29, 27, BLACK)
    i.frame(11, 6, 20, 10, BLACK)
    i.rect(2, 16, 29, 16, BLACK)
    i.rect(14, 15, 17, 18, YELLOW)


def i32(i):  # door
    i.rect(7, 2, 25, 29, OLIVE)
    i.frame(7, 2, 25, 29, BLACK)
    i.rect(10, 5, 22, 29, TEAL)
    i.frame(10, 5, 22, 29, BLACK)
    i.rect(13, 8, 19, 14, CYAN)
    i.disc(19, 18, 1, YELLOW)


def i33(i):  # parcel
    i.poly([(3, 11), (16, 6), (29, 11), (16, 16)], YELLOW, BLACK)
    i.poly([(3, 11), (16, 16), (16, 29), (3, 24)], OLIVE, BLACK)
    i.poly([(16, 16), (29, 11), (29, 24), (16, 29)], MAROON, BLACK)
    i.line(9, 8, 22, 14, RED)
    i.line(22, 8, 9, 14, RED)


def i34(i):  # mailbox with its flag
    i.rect(14, 18, 16, 29, GRAY)
    i.poly([(4, 18), (4, 10), (7, 6), (24, 6), (27, 10), (27, 18)], BLUE, BLACK)
    i.rect(6, 12, 10, 16, BLACK)
    i.rect(27, 4, 28, 13, BLACK)
    i.rect(29, 4, 31, 7, RED)


def i35(i):  # safe
    i.rect(3, 3, 28, 28, GRAY)
    i.frame(3, 3, 28, 28, BLACK)
    i.frame(6, 6, 25, 25, BLACK)
    i.disc(16, 15, 5, SILVER)
    i.ring(16, 15, 5, BLACK)
    i.line(16, 15, 19, 12, BLACK)
    i.rect(13, 22, 19, 23, BLACK)


def i36(i):  # help: a question mark
    i.poly([(9, 10), (11, 5), (16, 3), (21, 5), (23, 10), (21, 15), (18, 17), (18, 21),
            (14, 21), (14, 15), (18, 11), (17, 8), (15, 8), (13, 11)], YELLOW, BLACK)
    i.rect(14, 24, 18, 28, YELLOW)
    i.frame(14, 24, 18, 28, BLACK)


def i37(i):  # attention: an exclamation mark
    i.poly([(13, 3), (19, 3), (18, 20), (14, 20)], FUCHSIA, BLACK)
    i.disc(16, 26, 3, FUCHSIA)
    i.ring(16, 26, 3, BLACK)


def i38(i):  # exit: leaving by a door
    i32(i)
    i.poly([(2, 15), (12, 15), (12, 11), (19, 17), (12, 23), (12, 19), (2, 19)], RED, BLACK)


def point(i, flip):  # a pointing hand, cuff on the far side
    pts = [(2, 12), (8, 12), (11, 10), (28, 10), (29, 12), (17, 13), (17, 15), (20, 16),
           (19, 19), (16, 21), (11, 22), (8, 21), (2, 21)]
    if flip:
        pts = [(31 - x, y) for x, y in pts]
    i.poly(pts, WHITE, BLACK)
    if flip:
        i.rect(25, 12, 29, 21, NAVY)
    else:
        i.rect(2, 12, 6, 21, NAVY)


def i39(i):  # pointing right
    point(i, False)


def i40(i):  # pointing left
    point(i, True)


def i41(i):  # stop: a raised hand
    i.poly([(9, 14), (9, 5), (12, 5), (12, 12), (13, 3), (16, 3), (16, 12), (17, 4),
            (20, 4), (20, 13), (21, 7), (24, 7), (24, 20), (21, 25), (12, 25), (6, 18),
            (6, 14)], WHITE, BLACK)
    i.rect(11, 25, 22, 29, RED)
    i.frame(11, 25, 22, 29, BLACK)


def arrow(i, pts):
    i.poly(pts, SILVER, BLACK)


def i42(i):  # down
    arrow(i, [(11, 3), (20, 3), (20, 15), (27, 15), (16, 28), (4, 15), (11, 15)])


def i43(i):  # left
    arrow(i, [(28, 11), (28, 20), (16, 20), (16, 27), (3, 16), (16, 4), (16, 11)])


def i44(i):  # right
    arrow(i, [(3, 11), (3, 20), (15, 20), (15, 27), (28, 16), (15, 4), (15, 11)])


def i45(i):  # up
    arrow(i, [(11, 28), (20, 28), (20, 16), (27, 16), (16, 3), (4, 16), (11, 16)])


CLEAR_ = None
STOCK = [globals()['i%02d' % n] for n in range(46)]


def make(n):
    i = Icon()
    STOCK[n](i)
    return i
