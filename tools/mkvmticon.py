#!/usr/bin/env python3
"""mkvmticon - the VM Task Manager's icon (VMTASK.EXE, ARCHITECTURE.md
15.12): our own artwork (ARCHITECTURE.md 6.8), drawn with mkicons.Icon: a
small window whose body is a black graph panel with a green trace of the
processor's use, and below it three bars of different lengths, one per
virtual machine.

usage: mkvmticon.py OUT.ICO
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkicons import Icon, BLACK, WHITE, GRAY, SILVER, NAVY, LIME, GREEN, TEAL, BLUE, RED  # noqa: E402


def vmtask():
    i = Icon()
    # the window and its shadow
    i.rect(3, 3, 30, 30, GRAY)
    i.window(1, 1, 28, 28, NAVY, SILVER)
    # the graph panel, sunken
    i.rect(3, 8, 26, 17, BLACK)
    i.rect(3, 8, 26, 8, GRAY)
    i.rect(3, 8, 3, 17, GRAY)
    for x in (9, 15, 21):
        i.rect(x, 9, x, 17, GREEN)
    i.rect(4, 13, 26, 13, GREEN)
    trace = [16, 15, 15, 12, 10, 11, 14, 15, 13, 10, 9, 10, 12, 15, 16, 14,
             11, 10, 10, 12, 14, 13, 12]
    for k in range(len(trace) - 1):
        i.line(4 + k, trace[k], 5 + k, trace[k + 1], LIME)
    # one bar per VM
    i.rect(4, 20, 22, 21, BLUE)
    i.rect(4, 23, 9, 24, TEAL)
    i.rect(4, 26, 14, 27, RED)
    i.rect(4, 20, 4, 27, BLACK)
    return i


if __name__ == '__main__':
    with open(sys.argv[1], 'wb') as fh:
        fh.write(vmtask().ico())
