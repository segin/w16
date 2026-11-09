#!/usr/bin/env python3
"""vxdmap.py - the VxD recreation map for w16's WIN386.EXE.

Runs readexe (with w16's LE/DDB extension, build/readexe) over WIN386.EXE
and external .386 files and turns its text output into Markdown tables:
per-VxD header and DDB facts, and the service-usage matrix (which module
calls which VxD service through an INT 20h dynamic link).

Only header and table data is read: the LE header, object table, entry
table, fixups, the Device Descriptor Block, and the dword that follows each
INT 20h (CD 20) in an executable object, which the Windows 3.1 DDK documents
as the dynamic-link service ID (high word device ID, low word service
number, bit 15 = jump). Nothing is disassembled. A CD 20 byte pair that is
not a dynamic link (e.g. inside an instruction or data) is filtered out when
its device ID is not one of the known devices or its service number is not
below that device's service count (DDB_Service_Table_Size); the number of
sites dropped this way is reported.

Service names come from Ralf Brown's Interrupt List release 61, INT 20h
tables 01265 ff. (INTERRUP.F), if a path to it is given; that list is
documentation of the published dynamic-link numbers.

Usage: vxdmap.py READEXE RBIL_INTERRUP_F OUT.md LABEL:FILE [LABEL:FILE ...]
"""
import re
import subprocess
import sys
from collections import defaultdict, OrderedDict


def run_readexe(readexe, path):
    r = subprocess.run([readexe, '-s', path], capture_output=True, text=True,
                       errors='replace')
    return r.stdout


def parse_modules(text, fallback_name):
    """Split readexe output into LE modules and parse each one."""
    mods = []
    parts = re.split(r'\n(?=W3 module \[[0-9a-f]+\] )', text)
    if len(parts) == 1:
        parts = [text]
        w3 = False
    else:
        parts = parts[1:]
        w3 = True
    for p in parts:
        if 'Linear Executable with magic' not in p:
            continue
        m = {'w3': w3, 'objects': [], 'int20': [], 'ddb': {}}
        mm = re.match(r'W3 module \[([0-9a-f]+)\] (\S+): Linear Executable header at offset 0x([0-9a-f]+)', p)
        if mm:
            m['w3index'] = int(mm.group(1), 16)
            m['name'] = mm.group(2)
            m['offset'] = int(mm.group(3), 16)
        else:
            m['name'] = fallback_name
        r = re.search(r'Resident names:\n  \[  0\] (\S+)', p)
        m['resname'] = r.group(1) if r else ''
        r = re.search(r'VxD device ID:\t+0x([0-9a-f]+)', p)
        m['hdrid'] = int(r.group(1), 16) if r else None
        for o in re.finditer(r'^  (\d+)  ([0-9a-f]{8})  ([0-9a-f]{8})  ([0-9a-f]{8})  (\d+) \((\d+)\)  (.*)$', p, re.M):
            m['objects'].append({'n': int(o.group(1)), 'vsize': int(o.group(2), 16),
                                 'base': int(o.group(3), 16), 'flags': int(o.group(4), 16),
                                 'pages': int(o.group(5)), 'desc': o.group(7).strip()})
        r = re.search(r'Fixups:\t+(\d+) internal, (\d+) import by ordinal, (\d+) import by name, (\d+) internal entry', p)
        m['fixups'] = tuple(int(x) for x in r.groups()) if r else (0, 0, 0, 0)
        d = re.search(r'VxD Device Descriptor Block.*?\n((?:  .*\n?)+)', p)
        if d:
            for line in d.group(1).splitlines():
                k, _, v = line.strip().partition(':')
                m['ddb'][k.strip()] = v.strip()
        for s in re.finditer(r'^  (\d+):([0-9a-f]{8})  device 0x([0-9a-f]{4}) service 0x([0-9a-f]{4}) (call|jmp)$', p, re.M):
            m['int20'].append((int(s.group(1)), int(s.group(2), 16), int(s.group(3), 16),
                               int(s.group(4), 16), s.group(5)))
        mods.append(m)
    return mods


def parse_rbil(path):
    names = {}      # id -> {svc: text}
    devnames = {}   # id -> name
    if not path:
        return names, devnames
    cur = None
    with open(path, encoding='latin-1') as f:
        for line in f:
            h = re.match(r'Values for (.+?) \(VxD ID ([0-9A-F]{4})h\)\s*service number:', line)
            if h:
                cur = int(h.group(2), 16)
                names[cur] = {}
                devnames[cur] = h.group(1)
                continue
            if cur is None:
                continue
            s = re.match(r' ([0-9A-F]{4})h\t(.*)', line)
            if s:
                names[cur][int(s.group(1), 16)] = s.group(2).strip().strip('"')
            elif line.startswith('SeeAlso') or line.startswith('--------'):
                cur = None
    return names, devnames


def hexid(x):
    return '%04Xh' % x


def objsummary(objs):
    out = []
    for o in objs:
        f = o['flags']
        kind = 'code' if f & 4 else 'data'
        extra = []
        if f & 0x10:
            extra.append('discard')
        if f & 0x40:
            extra.append('preload')
        if f & 2:
            extra.append('write')
        out.append('%X %s%s' % (o['vsize'], kind, (' ' + ','.join(extra)) if extra else ''))
    return '; '.join(out)


def main():
    readexe, rbilpath, outpath = sys.argv[1:4]
    inputs = sys.argv[4:]
    svcnames, rbildev = parse_rbil(rbilpath if rbilpath != '-' else None)
    installs = OrderedDict()
    for spec in inputs:
        label, _, path = spec.partition(':')
        fname = path.rsplit('/', 1)[-1]
        mods = parse_modules(run_readexe(readexe, path), fname)
        for m in mods:
            m['file'] = fname
        installs.setdefault(label, []).extend(mods)

    out = []
    w = out.append
    w('<!-- Generated by tools/vxdmap.py from readexe output; do not edit by hand. -->')
    w('')
    for label, mods in installs.items():
        # Known devices for this install: id -> (name, service count)
        known = {}
        for m in mods:
            dd = m['ddb']
            if 'Device ID' in dd:
                did = int(dd['Device ID'].split()[0], 16)
                if did:
                    nm, cnt = dd['Device name'].strip('"').strip(), int(dd.get('Services', '0'))
                    if did in known and known[did][0] != nm:
                        # Two modules with one ID (BLOCKDEV and IOS.386 in WfW 3.11)
                        nm, cnt = known[did][0] + '/' + nm, max(cnt, known[did][1])
                    known[did] = (nm, cnt)
        w('## %s: modules' % label)
        w('')
        w('Device ID, version, init order and service count are the DDB fields; "API" lists the V86 and PM API procedures the DDB declares (INT 2Fh AX=1684h gives callers their entry points). Objects: virtual size in hex and kind. Fixups: internal fixup count (VxDs have no imports; calls to other VxDs are INT 20h dynamic links, counted in the last column after filtering).')
        w('')
        w('| File | Module | DDB name | ID | Ver | Init order | Services | Control | API | Objects | Fixups | INT 20h links |')
        w('|---|---|---|---|---|---|---|---|---|---|---|---|')
        for m in mods:
            dd = m['ddb']
            if dd:
                did = int(dd['Device ID'].split()[0], 16)
                api = []
                if dd.get('V86 API procedure', 'none') != 'none':
                    api.append('V86')
                if dd.get('PM API procedure', 'none') != 'none':
                    api.append('PM')
                row = [m['file'], m['name'], dd['Device name'].strip('"').strip(), hexid(did) if did else '(none)',
                       dd['Device version'], dd['Init order'].replace('0x', '') + 'h',
                       dd['Services'], dd['Control procedure'], ' '.join(api) or '-']
            else:
                row = [m['file'], m['name'], '(no DDB)', '', '', '', '', '', '']
            good = [s for s in m['int20'] if s[2] in known and s[3] < known[s[2]][1]]
            m['links'] = good
            m['dropped'] = len(m['int20']) - len(good)
            row += [objsummary(m['objects']), str(m['fixups'][0]),
                    '%d%s' % (len(good), (' (%d dropped)' % m['dropped']) if m['dropped'] else '')]
            w('| ' + ' | '.join(row) + ' |')
        w('')

        # Caller x callee device matrix
        callees = sorted({s[2] for m in mods for s in m['links']})
        w('## %s: which module calls which VxD' % label)
        w('')
        w('Number of distinct services of the column device that the row module calls (VMMcall/VxDcall/VxDjmp sites).')
        w('')
        w('| Caller | ' + ' | '.join(known.get(c, (hexid(c), 0))[0] for c in callees) + ' |')
        w('|---|' + '---|' * len(callees))
        for m in mods:
            cnt = defaultdict(set)
            for s in m['links']:
                cnt[s[2]].add(s[3])
            name = m['ddb'].get('Device name', m['name']).strip('"').strip() or m['name']
            w('| ' + name + ' | ' + ' | '.join(str(len(cnt[c])) if cnt[c] else '' for c in callees) + ' |')
        w('')

        # Per-service usage
        w('## %s: service usage' % label)
        w('')
        w('Every service called by any module of this set, with its callers (number of call sites in brackets; "j" marks a jump). Names from RBIL INT 20h tables where listed.')
        w('')
        use = defaultdict(lambda: defaultdict(lambda: [0, False]))
        for m in mods:
            caller = m['ddb'].get('Device name', m['name']).strip('"').strip() or m['name']
            for s in m['links']:
                e = use[(s[2], s[3])][caller]
                e[0] += 1
                if s[4] == 'jmp':
                    e[1] = True
        for dev in callees:
            dname, dcount = known.get(dev, (hexid(dev), 0))
            used = sorted(k[1] for k in use if k[0] == dev)
            w('### %s (%s): %d of %d services used' % (dname, hexid(dev), len(used), dcount))
            w('')
            w('| Service | Name (RBIL) | Callers |')
            w('|---|---|---|')
            for sv in used:
                callers = use[(dev, sv)]
                cs = ', '.join('%s (%d%s)' % (c, n, ' j' if j else '') for c, (n, j) in sorted(callers.items()))
                w('| %04Xh | %s | %s |' % (sv, svcnames.get(dev, {}).get(sv, ''), cs))
            w('')
        unused = []
        for dev, (dname, dcount) in sorted(known.items()):
            n = len({k[1] for k in use if k[0] == dev})
            unused.append('%s %d/%d' % (dname, n, dcount))
        w('Services used / provided, per device: ' + ', '.join(unused) + '.')
        w('')
    with open(outpath, 'w') as f:
        f.write('\n'.join(out))


if __name__ == '__main__':
    main()
