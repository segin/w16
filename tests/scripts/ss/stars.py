"""stars.py SHOT.BMP DENSITY: does a screenshot look like the running
Starfield Simulation (tests/ssscenarios.sh)? A black screen whose only
other pixels are white squares of 1 to 4 pixels (stars; two that touch
count as one blob up to 8 x 8), at least a third as many as DENSITY and
at most as many. Prints what it found; exit status 1 if it does not."""
import sys
from PIL import Image

path, density = sys.argv[1], int(sys.argv[2])
im = Image.open(path).convert('RGB')
w, h = im.size
px = im.load()
seen = set()
blobs = []
bad = None
for y in range(h):
    for x in range(w):
        c = px[x, y]
        if c == (0, 0, 0) or (x, y) in seen:
            continue
        if c != (255, 255, 255):
            bad = bad or 'a pixel neither black nor white at %d,%d %s' % (x, y, c)
        st = [(x, y)]
        seen.add((x, y))
        pts = []
        while st:
            a, b = st.pop()
            pts.append((a, b))
            for da in (-1, 0, 1):
                for db in (-1, 0, 1):
                    q = (a + da, b + db)
                    if 0 <= q[0] < w and 0 <= q[1] < h and q not in seen and px[q] != (0, 0, 0):
                        seen.add(q)
                        st.append(q)
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
        if bw > 8 or bh > 8:
            bad = bad or 'a blob of %d x %d at %d,%d' % (bw, bh, min(xs), min(ys))
        blobs.append((bw, bh))
sizes = {}
for bw, bh in blobs:
    sizes[max(bw, bh)] = sizes.get(max(bw, bh), 0) + 1
msg = '%d stars, sizes %s' % (len(blobs), ' '.join('%d:%d' % kv for kv in sorted(sizes.items())))
if not bad and not density // 3 <= len(blobs) <= density:
    bad = '%d stars for a density of %d' % (len(blobs), density)
print(bad + ' (' + msg + ')' if bad else msg)
sys.exit(1 if bad else 0)
