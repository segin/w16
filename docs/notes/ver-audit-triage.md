# VER audit triage

An outside, read-only audit of `shell/ver/` (written by another AI model;
not in the repository) listed 2 High, 5 Medium and 5 Low findings
(H1-H2, M1-M5, L1-L5 here, in its order). Each is checked here against
the code, and against Windows 3.1 where the fix would change what a
program sees: the VER probe's new section `audit` (`tests/ver/vrp.c`,
letter `a`; its files by `tests/ver/mkinputs.py` in `C:\VT\AUD` and the
install pool), run as the shell of the reference 3.1 with Microsoft's
VER.DLL and with w16's (`tests/ver/vrun.sh`; the boot-test case `ver`
compares the two logs section by section, parts s, 3, tree286 and
tree386 include `audit`). The audit's line numbers were close.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong, or
the probe shows w16 already as 3.1), **3.1-BEHAVIOUR** (3.1 does the
same: kept), **HARDENING** (only with malformed input; fixed only when
cheap and invisible to working programs), **UNSURE** (needs a probe not
run yet). Where 3.1 itself ends the program, w16 may survive instead
(the user's ruling); in this audit 3.1's VER.DLL lived through every
case, so every case is compared with it.

Status: every finding has a verdict and is closed (2026-11-21).

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | REAL (in part 3.1-BEHAVIOUR) | *lpcb is wValueLength as it is on 3.1 too (raw blocks TRV100, TRVFFFF: 256 and 65535; RTVFFFF: 65535 for the root): kept. The fixed part is compared whatever the root's wValueLength (AVLEN0, AVLEN8, AVLEN40: VIF_SRCOLD, VIF_DIFFTYPE as with 52): kept. But 3.1 reads the resource table's length for the comparison, not the root's: a root of 20 bytes in a 384-byte resource (ALEN20) is compared by the real fixed part after it (VIF_SRCOLD for 3.0 over it, nothing for it over 3.0); w16 read 20 bytes into a 20-byte block and the fixed part from past it. A Translation of FFFFh or 100h bytes in the destination gives VIF_DIFFLANG, in the source nothing; one of 2 bytes in the source gives VIF_DIFFLANG (no entries), w16 skipped values under 4 bytes; one of 8 reaching past its node inside the block (ATRV8) is read as two entries (w16 already). Also: a root length of 8000h or more finds nothing on 3.1 (7FFFh finds; FFF8h-FFFFh nothing), w16 found everything; a block whose root runs past its segment (GOOD.BIN at the segment's end, root 400h) gives FALSE for a path past the segment, no fault. | getver reads the table length into a zeroed block with 64 bytes to spare; Translation entries past the block count as zero entries (the dest FFFFh/100h and source cases as 3.1); any Translation length compared; the root's length read as signed; VerQueryValue reads nothing past the block's segment (GetSelectorLimit). Probe: section audit (raw TRV*, RTVFFFF, roots 1000h-FFFFh, aseg blocks at a segment's end; install cases with AVLEN*, ALEN20/60/72, ATRV2/8/100/FFFF, ATAIL). | 43a17b4, 24b76ce |
| H2 | REAL | 3.1 reads a child's key no further than one byte before its parent's end: Translation's key running to its parent's end without a NUL (KEYEND, KEYEND0) matches neither "Translation" nor "TranslationQ"; with the parent one byte longer and "Q" in that byte (KEYENDQ) it matches "Translation"; with a NUL there (KEYEND1) "Translation". w16's lstrcmpi read past the parent (matched "TranslationQ" and "Translation" in KEYEND/KEYEND0) and could fault at a segment's end. | `keyis`: the key copied up to its NUL, one byte before the parent's end, or the segment's end, then lstrcmpi. Probe: raw KEYEND, KEYEND0, KEYEND1, KEYENDQ; KEYEND0 at its segment's end. | 43a17b4, 24b76ce |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | REAL (shift); FALSE (table offset 0) | 3.1 finds resources with alignment shifts up to 12 (offsets past the file's end too: SH12 gives F8000h) and none with 13 or more (SH13-SH65535; SZ13-SZ32 with the resource at 0, one unit long), the offset untouched. w16 gave lengths and offsets shifted by 13-17 bits and, past 31, found the resource at 0 with length 0. A resource table at the header's offset 0 (RT0): nothing found on both. | A shift over 12 finds nothing. Probe: SH1-SH65535, SZ1-SZ32, RT0. | 43a17b4, 24b76ce |
| M2 | REAL | TEMP.000-TEMP.FFF all there: 3.1 returns VIF_CANNOTCREATE, no file touched, the buffer not written. w16 overwrote TEMP.FFF and went on (VIF_SRCOLD, "temp.FFF"). | VIF_CANNOTCREATE when all 4096 names are taken. Probe: the last audit case (4096 empty files; each checked still empty). | 43a17b4, 24b76ce |
| M3 | REAL | A rename that fails (the destination name in a directory that is not there, "NODIR\T.DLL"): 3.1 returns VIF_CANNOTRENAME alone and deletes the temporary file, also the caller's TEMP.000 in a forced second call. w16 left it. | The temporary file deleted. Probe: four NODIR cases (plain, a difference, forced, second call). | 43a17b4, 24b76ce |
| M4 | REAL | 3.1 does not search for temporary names: with an empty destination directory (the current one) and TEMP.000 only in a PATH directory it uses temp.000, and a forced second call naming TEMP.000 there reads the source again. A directory named TEMP.000 in the destination directory is skipped (temp.001), and as a second call's temporary name it is "installed" (T.DLL deleted, the directory renamed to it). w16 used OpenFile's OF_EXIST: it searched the PATH (temp.001; the second call found the PATH's file and failed to rename it) and did not see directories (VIF_CANNOTCREATE). | Existence by MS-DOS's attributes (INT 21h 4300h) for both. Probe: the TEMP.000 directory and empty-directory cases. | 43a17b4, 24b76ce |
| M5 | REAL | 3.1 takes a NULL lpszTmpFile: forced, the source is installed (0); with a difference VIF_SRCOLD \| VIF_MISMATCH \| VIF_TEMPFILE, length 9, the file kept; and a NULL lpwTmpFileLen as room enough (the name written, VIF_TEMPFILE). w16 dereferenced both. | NULL checks: no second call without a name, nothing written through NULL. Probe: four NULL cases. | 43a17b4, 24b76ce |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | 3.1-BEHAVIOUR | valoff reads a matched child's key up to its parent's end, not its own: 3.1 does (KEYENDQ: the value at 380, the "Q" in the child's key; the query section's CH12, a CompanyName node of 12 bytes, as 3.1). | Kept. | - |
| L2 | REAL (by reading) | `shared_windows` took a Windows directory at a drive's root ("C:\", system directory "C:\SYSTEM") for a shared copy, so VerFindFile would recommend the Windows directory for shared files. Not observable on the reference install (Windows in C:\WINDOWS); the shared-copy test itself stays UNVERIFIED (ARCHITECTURE.md 20.7). | The root's backslash dropped before comparing. | 24b76ce |
| L3 | REAL | 3.1's GetFileVersionInfoSize(name, NULL) and GetFileResourceSize(..., NULL) return the size (384); w16 wrote through NULL. | NULL checks. Probe: both calls. | 43a17b4, 24b76ce |
| L4 | 3.1-BEHAVIOUR | GetFileVersionInfoSize reads 24 bytes whatever the resource's length: 3.1 too. A table length of 20 (TBL20) or 8 (TBL8) with the rest of the block after it in the file: size 20 or 8 (the signature read past the resource); the file ending 20 or 22 bytes into the resource (CUT20, CUT22): 0, the handle set. w16 the same. | Kept. | - |
| L5 | FALSE | VER's data segment after loading has lock count 0 under both libraries (GlobalFlags of its instance), so nothing stays locked. LocalInit's result is not checked, but VER.DLL never uses its local heap. | None. | - |

## Beyond the audit

Found by the probe and fixed in 24b76ce: a root length of 8000h or more
finds nothing (H1); a Translation under 4 bytes is compared (H1); the
version comparison reads the resource table's length (H1); a directory
named as the second call's temporary file is installed (M4).

Kept on purpose: with a table length of 20 and a root of 20 (no fixed
part in the resource), 3.1 compares whatever its buffer held before (two
runs gave VIF_DIFFTYPE and nothing); w16 compares zeros. The probe leaves
that case out.
