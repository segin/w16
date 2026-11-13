#!/bin/sh
# COMMDLG regression against the original (ARCHITECTURE.md 17): each
# scenario tests/scripts/commdlg/<name>.txt runs on the reference Windows
# 3.1 twice, with Microsoft's COMMDLG.DLL and with w16's
# (build/dos/COMMDLG.DLL copied over WINDOWS\SYSTEM\COMMDLG.DLL in the
# scratch copy). A scenario is a SNAP script (tests/apps/snap.c) whose
# lines starting "cd1 " go to CD1.TXT for the probe tests/apps/cd1.c,
# which SNAP starts first unless the scenario has "nocd1"; lines "file
# <host path>=<path under C:>" copy extra files in, "ini <file>
# <SECTION:KEY=VALUE>" sets an .INI entry (tests/scripts/iniset.py),
# "expand <name> <path under C:>" expands a compressed file of the Setup
# disks (tools/szdd.py; printer drivers). Compared afterwards: CD1.LOG (the
# returned structures; "ignore <regex>" lines exclude log lines from the
# comparison, "nohwnd" masks window handles in the hook's lines) and every
# screenshot, which may differ from the original by
# no more pixels than tests/scripts/commdlg.baseline records
# ("scenario/SHOT count"; our own drive and folder pictures).
#   usage: tests/commdlgscen.sh [-n] [scenario...]       (default: all)
#   -n: the original only (observation; nothing is compared)
#   CD_OUT=dir: where the runs go (default build/ref/commdlg-<timestamp>)
#   CD_BASELINE=update: rewrite the baseline from this run
#   CD_DLL, CD_PROBE, CD_SNAP: our COMMDLG.DLL, CD1.EXE, SNAP.EXE (default
#   build/dos/...)
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
origonly=
if [ "${1:-}" = -n ]; then origonly=1; shift; fi
out=${CD_OUT:-$root/build/ref/commdlg-$(date +%Y%m%d-%H%M%S)}
names=${*:-$(cd "$root/tests/scripts/commdlg" && ls *.txt | sed 's/\.txt$//')}
base=$root/tests/scripts/commdlg.baseline
[ -f "$base" ] || : > "$base"
ref=$root/.tools/ref/win31/cdrive
status=0
newbase=$(mktemp); trap 'rm -f "$newbase"' EXIT
for n in $names; do
    src=$root/tests/scripts/commdlg/$n.txt
    whos="orig w16"
    [ -n "$origonly" ] && whos=orig
    for who in $whos; do
        d=$out/$n/$who
        mkdir -p "$d"
        rm -f "$d"/*
        # a scratch reference install with this scenario's changes
        work=$(mktemp -d)
        cp -r "$ref" "$work/c"
        files="$d/SNAP.TXT $d/CD1.TXT ${CD_PROBE:-$root/build/dos/CD1.EXE}"
        grep -q '^nocd1' "$src" || echo 'run C:\CD1.EXE' | sed 's/$/\r/' > "$d/SNAP.TXT"
        grep -v '^cd1 \|^file \|^ini \|^expand \|^ignore \|^nocd1\|^nohwnd' "$src" | sed 's/$/\r/' >> "$d/SNAP.TXT"
        grep '^cd1 ' "$src" | sed 's/^cd1 //; s/$/\r/' > "$d/CD1.TXT"
        grep '^file ' "$src" | sed 's/^file //' | while read -r f; do
            mkdir -p "$(dirname "$work/c/${f#*=}")"
            cp "$root/${f%%=*}" "$work/c/${f#*=}"
        done
        grep '^ini ' "$src" | sed 's/^ini //' | while read -r ini spec; do
            python3 "$root/tests/scripts/iniset.py" "$work/c/$ini" "$spec"
        done
        grep '^expand ' "$src" | sed 's/^expand //' | while read -r f to; do
            python3 "$root/tools/szdd.py" "$root/.tools/ref/win31/src/$f" "$work/c/$to"
        done
        if [ $who = w16 ]; then
            cp "${CD_DLL:-$root/build/dos/COMMDLG.DLL}" "$work/c/WINDOWS/SYSTEM/COMMDLG.DLL"
        fi
        REFRUN_FILES="$files" REFRUN_OUT=$d REFRUN_GET="CD1.LOG" \
            "$root/tests/refrun.sh" "${CD_SNAP:-$root/build/dos/SNAP.EXE}" /S "$work/c" > "$d/log.txt" || true
        rm -rf "$work"
    done
    [ -n "$origonly" ] && { echo "== $n: observed ($out/$n/orig)"; continue; }
    result=ok
    ign=$(grep '^ignore ' "$src" | sed 's/^ignore //' | paste -sd'|' -)
    # "nohwnd": window handles in the hook's lines are not compared
    hw=cat
    grep -q '^nohwnd' "$src" && hw="sed -e s/WM_INITDIALOG.wp=..../WM_INITDIALOG_wp=hwnd/ -e s/\(WM_COMMAND.wp=.....lp=....\)..../\1hwnd/"
    filt() { if [ -n "$ign" ]; then tr -d '\r' < "$1" | grep -Ev "$ign"; else tr -d '\r' < "$1"; fi | $hw; }
    if [ -f "$out/$n/orig/CD1.LOG" ]; then
        if ! [ -f "$out/$n/w16/CD1.LOG" ] || ! filt "$out/$n/orig/CD1.LOG" > "$out/$n/orig.log" ||
           ! filt "$out/$n/w16/CD1.LOG" > "$out/$n/w16.log" ||
           ! diff "$out/$n/orig.log" "$out/$n/w16.log" > "$out/$n/log.diff"; then
            result=FAIL; echo "  $n: CD1.LOG differs ($out/$n/log.diff)"
        fi
    fi
    shots=""
    for f in "$out/$n"/orig/*.BMP; do
        [ -f "$f" ] || continue
        s=$(basename "$f" .BMP)
        if [ ! -f "$out/$n/w16/$s.BMP" ]; then
            result=FAIL; echo "  $n: screenshot $s missing"; continue
        fi
        px=$(magick compare -metric AE "$f" "$out/$n/w16/$s.BMP" "$out/$n/$s-diff.png" 2>&1 | cut -d' ' -f1 | cut -d. -f1)
        shots="$shots $s=$px"
        echo "$n/$s $px" >> "$newbase"
        allowed=$(awk -v k="$n/$s" '$1 == k { print $2 }' "$base")
        if [ -z "$allowed" ]; then
            echo "  $n: $s has no baseline ($px pixels differ)"
            [ "${CD_BASELINE:-}" = update ] || result=FAIL
        elif [ "$px" -gt "$allowed" ]; then
            result=FAIL; echo "  $n: $s differs by $px pixels, baseline $allowed"
        elif [ "$px" -lt "$allowed" ]; then
            echo "  $n: $s improved: $px pixels (baseline $allowed)"
        fi
    done
    echo "== $n: $result (pixels:$shots)"
    [ $result = ok ] || status=1
done
if [ "${CD_BASELINE:-}" = update ]; then
    awk 'NR == FNR { seen[$1] = 1; print; next } !($1 in seen)' "$newbase" "$base" \
        | sort > "$base.tmp" && mv "$base.tmp" "$base"
    echo "baseline updated: $base"
fi
echo "runs: $out"
exit $status
