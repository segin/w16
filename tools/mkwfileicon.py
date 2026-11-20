#!/usr/bin/env python3
"""mkwfileicon - w16 File Manager's icons: our own artwork (ARCHITECTURE.md
6.8), drawn with mkicons.Icon.
  winfile.ico  the program: a two-drawer filing cabinet, a folder on top
  tree.ico     a minimised directory window showing the tree only
  dir.ico      ... the file list only (and a Search Results window)
  treedir.ico  ... both
  (each a small window: a title bar over the panes it shows)

usage: mkwfileicon.py OUTDIR
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, WHITE, GRAY, SILVER, YELLOW, OLIVE, NAVY, TEAL, CYAN  # noqa: E402


def folder(i, x, y):
    """a 12 x 9 folder with its top-left at x, y"""
    i.rect(x + 1, y, x + 5, y, BLACK)
    i.frame(x, y + 1, x + 11, y + 8, BLACK)
    i.rect(x + 1, y + 2, x + 10, y + 7, YELLOW)
    i.rect(x + 1, y + 7, x + 10, y + 7, OLIVE)


def cabinet():
    i = Icon()
    # the cabinet
    i.rect(5, 8, 26, 30, SILVER)
    i.frame(5, 8, 26, 30, BLACK)
    i.rect(6, 9, 6, 29, WHITE)
    i.rect(25, 9, 25, 29, GRAY)
    for top in (10, 20):
        i.frame(8, top, 23, top + 8, BLACK)
        i.rect(9, top + 1, 22, top + 7, GRAY)
        i.rect(13, top + 3, 18, top + 4, BLACK)
        i.rect(14, top + 3, 17, top + 3, WHITE)
    # a folder lying on top
    folder(i, 10, 0)
    return i


def window(i):
    """the frame of a small window with a teal title bar"""
    i.rect(1, 3, 30, 28, WHITE)
    i.frame(0, 2, 31, 29, BLACK)
    i.rect(1, 3, 30, 5, TEAL)
    i.rect(1, 6, 30, 6, BLACK)


def tree_pane(i, x0):
    """a small folder tree from x0"""
    i.rect(x0 + 2, 10, x0 + 2, 24, BLACK)
    for k, y in enumerate((8, 14, 20)):
        if k:
            i.rect(x0 + 2, y + 3, x0 + 4, y + 3, BLACK)
        fx = x0 + (0 if k == 0 else 5)
        i.rect(fx, y, fx + 6, y + 4, YELLOW)
        i.frame(fx, y, fx + 6, y + 4, BLACK)


def list_pane(i, x0, x1):
    """lines of a file list between x0 and x1"""
    for y in range(9, 27, 4):
        i.rect(x0, y, x0 + 2, y + 2, NAVY)
        i.rect(x0 + 4, y + 1, x1, y + 1, BLACK)


def tree():
    i = Icon()
    window(i)
    tree_pane(i, 4)
    return i


def dirlist():
    i = Icon()
    window(i)
    list_pane(i, 4, 26)
    return i


def treedir():
    i = Icon()
    window(i)
    tree_pane(i, 2)
    i.rect(15, 7, 15, 28, BLACK)
    list_pane(i, 18, 28)
    return i


if __name__ == '__main__':
    out = sys.argv[1]
    for name, f in (('winfile.ico', cabinet), ('tree.ico', tree), ('dir.ico', dirlist),
                    ('treedir.ico', treedir)):
        with open(os.path.join(out, name), 'wb') as fh:
            fh.write(f().ico())
