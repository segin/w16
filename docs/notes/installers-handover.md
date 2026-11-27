# Installers: handover (2026-11-25)

What was found and fixed is in ARCHITECTURE.md 9.12; the regression
test is `tests/installers.sh` (boot-test cases `installers`, `nametable`,
`dlgfocus`). Open items:

1. **WinPlay3 2.3 beta 5 (WP3V23B5.EXE) and the MPEG player beta
   (MP52BETA.EXE)**: done (2026-11-26). Blinker's loader read BX, CX and
   ES after EntryAddrProc; the self-loading probe TK78 (boot-test case
   `selfload`) recorded what 3.1's KERNEL gives a self-loader
   (ARCHITECTURE.md 9.12). Both run in `tests/installers.sh` (WP3, MP52).
2. **"Cannot decompress file" (WEP4, Best of)**: not reproduced. With the
   disks in `..\DISK1`, `..\DISK2` as Setup's INF expects, both install
   on w16 as on 3.1. Ask the user how the disks were laid out. Whether
   3.1's LZEXPAND reads KWAJ files is UNVERIFIED (w16's reads SZDD only).
3. w16's Program Manager ignores a program named on its command line
   (`WIN /3 PROGRAM.EXE`); 3.1's runs it.
