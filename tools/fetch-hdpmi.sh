#!/bin/sh
# Fetch the HX runtime (for HDPMI16.EXE, a freeware 16-bit DPMI host) into
# .tools/hx. Test-only dependency; never committed or redistributed.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
d="$root/.tools/hx"; mkdir -p "$d"; cd "$d"
url=https://github.com/Baron-von-Riedesel/HX/releases/download/v2.23/HXRT223.zip
sum=20510cf66d6c8704f66f908f8fbf9783198195c1e043a6ca9fb6adeadf8785c9
curl -sSLo HXRT223.zip "$url"
echo "$sum  HXRT223.zip" | sha256sum -c -
rm -rf hxrt && unzip -q HXRT223.zip -d hxrt
