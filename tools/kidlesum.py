#!/usr/bin/env python3
"""kidlesum.py COM1.TXT: VXT.386's reports (tests/vxd/vxt.asm) summed per
phase of VX17 (tests/apps/vx17.c, tests/kidlerun.sh; ARCHITECTURE.md 12.15).

VX17 marks each phase with INT 2Fh AX=B171h-B174h at its start and B17Fh
at its end; VXT reports at each mark. A phase's figures are the reports
after its start mark's own (which closes what came before) up to and
including the end mark's. Per phase: its length (ms, the VMM's clock),
the Call_When_Idle callbacks, and per VM its run time (ms and per cent of
the phase), the switches to it, V86 INT 28h, INT 2Fh 1689h (and those with
BX=0), 1680h, Release_Time_Slice while it ran; the rates per second.
"""
import re
import sys

NAMES = {1: "idle", 2: "tick", 3: "busy", 4: "msgs"}


def main():
    lines = open(sys.argv[1], errors="replace").read().splitlines()
    phases = []
    cur = None          # the phase being summed
    skip = False        # the next report closes the time before a phase
    last = False        # the next report is the phase's last
    inrep = None        # the report being read belongs to cur
    for ln in lines:
        m = re.match(r"VXT mark 0000B17([0-9A-F])", ln)
        if m:
            n = int(m.group(1), 16)
            if n == 15:
                last = True
            else:
                cur = {"name": NAMES.get(n, str(n)), "ms": 0, "cwi": 0, "vm": {}}
                phases.append(cur)
                skip = True
            continue
        m = re.match(r"VXT t\+([0-9A-F]+) scheduled=\S+ idle=\S+ cwi=([0-9A-F]+)", ln)
        if m:
            if skip:
                skip = False
                inrep = None
            elif cur is not None:
                inrep = cur
                cur["ms"] += int(m.group(1), 16)
                cur["cwi"] += int(m.group(2), 16)
                if last:
                    last = False
                    cur = None
            else:
                inrep = None
            continue
        m = re.match(r"VXT vm=([0-9A-F]+) st=([0-9A-F]+) run=([0-9A-F]+) sw=([0-9A-F]+)"
                     r" k0/1/o=\S+ 1680=([0-9A-F]+) i28=([0-9A-F]+) 1689/bx0=([0-9A-F]+)/([0-9A-F]+)"
                     r" d0b=\S+ rts=([0-9A-F]+)", ln)
        if m and inrep is not None:
            vid = int(m.group(1), 16)
            v = inrep["vm"].setdefault(vid, {"run": 0, "sw": 0, "p80": 0, "i28": 0, "p89": 0,
                                             "bx0": 0, "rts": 0, "st": m.group(2)})
            for k, g in (("run", 3), ("sw", 4), ("p80", 5), ("i28", 6), ("p89", 7),
                         ("bx0", 8), ("rts", 9)):
                v[k] += int(m.group(g), 16)
    seen = {}
    for p in phases:
        seen[p["name"]] = seen.get(p["name"], 0) + 1
        s = p["ms"] / 1000.0 or 1
        print("VXTSUM phase=%s#%d ms=%d cwi=%d (%.0f/s)" % (p["name"], seen[p["name"]], p["ms"],
                                                           p["cwi"], p["cwi"] / s))
        for vid in sorted(p["vm"]):
            v = p["vm"][vid]
            print("VXTSUM   vm=%d run=%d (%.0f%%) sw=%d i28=%d (%.0f/s) 1689=%d (%.0f/s, bx0 %d)"
                  " 1680=%d rts=%d (%.0f/s) st=%s"
                  % (vid, v["run"], 100.0 * v["run"] / (p["ms"] or 1), v["sw"], v["i28"],
                     v["i28"] / s, v["p89"], v["p89"] / s, v["bx0"], v["p80"], v["rts"],
                     v["rts"] / s, v["st"]))


if __name__ == "__main__":
    main()
