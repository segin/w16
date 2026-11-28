#!/usr/bin/env python3
"""atmfonts.py ATM.INI WINDIR - add Acrobat Reader's own ATM fonts to an
ATM.INI (tests/atm.sh): ZapfDingbats and the Multiple Master faces
AdobeSanMM and AdobeSerMM in [Fonts], the two MM faces in [MMFonts], their
files in WINDIR (e.g. C:\\W16\\) and WINDIR\\PFM, as Reader's Setup lists
them. The file keeps its CR LF lines."""
import sys

path, w = sys.argv[1], sys.argv[2]
add = {
    'Fonts': ['ZapfDingbats=%sPFM\\ZD______.PFM,%sZD______.PFB' % (w, w),
              'AdobeSanMM=%sPFM\\ZX______.PFM,%sZX______.PFB' % (w, w),
              'AdobeSerMM=%sPFM\\ZY______.PFM,%sZY______.PFB' % (w, w)],
    'MMFonts': ['AdobeSanMM=%sPFM\\ZX______.MMM,%sZX______.PFB' % (w, w),
                'AdobeSerMM=%sPFM\\ZY______.MMM,%sZY______.PFB' % (w, w)],
}
text = open(path, 'rb').read().decode('latin-1')
lines = []
for line in text.split('\r\n'):
    lines.append(line)
    t = line.strip()
    if t.startswith('[') and t.endswith(']') and t[1:-1] in add:
        lines.extend(add.pop(t[1:-1]))
for sec, entries in add.items():        # a section the file lacks
    lines += ['', '[%s]' % sec] + entries
open(path, 'wb').write('\r\n'.join(lines).encode('latin-1'))
