#!/usr/bin/env python3
"""iniedit - copy an .INI file with keys set or removed (test set-up).

    usage: tools/iniedit.py IN OUT  < edits

Each line of the standard input is "SECTION|KEY|VALUE" (the key set,
added at the end of its section, or the section added at the end of
the file) or "SECTION|KEY" (the key removed). Lines end CR LF as in
Windows' own INI files; names compare without case.
"""
import sys


def main():
    src, dst = sys.argv[1], sys.argv[2]
    lines = open(src, 'rb').read().decode('latin-1').replace('\r\n', '\n').split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    for edit in sys.stdin.read().splitlines():
        edit = edit.rstrip('\r')
        if not edit:
            continue
        parts = edit.split('|', 2)
        sec, key = parts[0], parts[1]
        value = parts[2] if len(parts) > 2 else None
        start = end = None
        for i, l in enumerate(lines):
            s = l.strip()
            if s.startswith('['):
                if start is not None and end is None:
                    end = i
                if s[1:].split(']')[0].lower() == sec.lower():
                    start = i
        if start is not None and end is None:
            end = len(lines)
        if start is None:
            if value is not None:
                lines += ['', '[%s]' % sec, '%s=%s' % (key, value)]
            continue
        at = None
        for i in range(start + 1, end):
            if lines[i].split('=')[0].strip().lower() == key.lower() and '=' in lines[i]:
                at = i
                break
        if value is None:
            if at is not None:
                del lines[at]
        elif at is not None:
            lines[at] = '%s=%s' % (key, value)
        else:
            last = end
            while last > start + 1 and not lines[last - 1].strip():
                last -= 1
            lines.insert(last, '%s=%s' % (key, value))
    open(dst, 'wb').write(('\r\n'.join(lines) + '\r\n').encode('latin-1'))


if __name__ == '__main__':
    main()
