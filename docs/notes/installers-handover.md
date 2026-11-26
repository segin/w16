# Installers: handover (2026-11-25)

What was found and fixed is in ARCHITECTURE.md 9.12; the regression
test is `tests/installers.sh` (boot-test cases `installers`, `nametable`,
`dlgfocus`). Open items:

1. **WinPlay3 2.3 beta 5 (WP3V23B5.EXE) and the MPEG player beta
   (MP52BETA.EXE)**: self-loading programs with Blinker's loader.
   `kernel/loader.c` boot_app runs BootApp and LoadAppSeg as the SDK
   documents. With `[w16] int21log=1`, the debuglog shows the
   following. MyAlloc(5150h, 0, 0) is answered with a handle only.
   Blinker GlobalReAllocs it to 4440h, takes a CS-to-DS alias, and reads
   segment 2 (2D03h bytes from 1558h, compressed) and its 88 relocation
   records (2C0h bytes). It resolves nine imports through EntryAddrProc
   (USER, GDI, KERNEL.49), then shows "BLINKER - Unknown relocation
   type". The next record is KERNEL.90 (lstrlen), which never reaches
   EntryAddrProc. Next steps:
   - write a self-loading probe (a NASM segment 1 with the loader table,
     BootApp and LoadAppSeg that log what KERNEL gives: MyAlloc's
     selectors and their access rights for code and data segments, the
     module table's segment entries, the table's other slots, Pietrek's
     fourth pointer);
   - run it on 3.1 (`tests/dropin.sh -r`) and on w16, and match the two;
   - suspects: the in-memory entry table or segment table of w16's
     KERNEL module, which Blinker may read itself for some imports; the
     selector type MyAlloc gives a code segment (Wine's MyAlloc16, X11
     era, makes it a code selector at once).
   To run it: `tests/installers.sh` has no WP3 script yet; start it by
   hand with File > Run as the other scripts do (the installers are kept
   on installation-05 in /mnt/build/w16-inst-apps).
2. **"Cannot decompress file" (WEP4, Best of)**: not reproduced. With the
   disks in `..\DISK1`, `..\DISK2` as Setup's INF expects, both install
   on w16 as on 3.1. Ask the user how the disks were laid out. Whether
   3.1's LZEXPAND reads KWAJ files is UNVERIFIED (w16's reads SZDD only).
3. w16's Program Manager ignores a program named on its command line
   (`WIN /3 PROGRAM.EXE`); 3.1's runs it.
