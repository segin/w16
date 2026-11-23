# Notepad audit triage

An outside, read-only audit of `shell/notepad/` (written by another AI
model; not in the repository) listed 2 Critical, 3 High, 5 Medium and 3
Low findings (C1-C2, H1-H3, M1-M5, L1-L3 here, in its order). Each is
checked here against the code, and against Windows 3.1 where a fix would
change what a user sees, with Notepad scenarios on the reference 3.1
(`tests/npscenarios.sh`; Microsoft's Notepad against w16's, both on 3.1's
USER, and with `NP_W16FILES` on w16's USER) and `tests/npqes.sh`
(whole-screen captures, for system-modal boxes). New for this: the
scenario scripts' `; expand` and `; ini` lines (a printer driver, Generic
/ Text Only, writing to a file) and SNAP's `postwin` (a message posted to
a window behind a modal dialog). The audit's line numbers were close.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong, or
cannot happen), **3.1-BEHAVIOUR** (3.1 does the same: kept),
**HARDENING** (only with malformed input; fixed only when cheap and
invisible), **UNSURE** (needs a probe not run yet). Where 3.1 itself fails,
w16 may survive instead (the user's ruling).

Status: every finding has a verdict and is closed (2026-11-22).

## Critical

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL on w16's USER; FALSE on 3.1's | `get_text` (file.c:47-55), `toggle_wrap` (main.c:173), `search` (find.c:47) and `print_file` (print.c:331) called `GetWindowText(g_edit, p, n + 1)` with a WORD n: 32767 or more is a negative int. 3.1's USER copies anyway: before the fix, w16's Notepad on 3.1 saved a 51192-byte text byte for byte as Microsoft's (NP11 first run, BIGOUT.TXT identical). w16's USER does not: `GetWindowText` returns 0 for `n <= 0` (user/win.c) and the edit control's WM_GETTEXT for `(int)wp <= 0` (user/edit.c), so on w16's USER (by reading) a save would write zeros, Word Wrap empty the text, Find find nothing, Print print nothing. | `edit_text` (main.c): the text read from the control's block (EM_GETHANDLE, LocalLock with DS switched to the block's segment, up to the control's length), as Microsoft's Notepad reads it; used by Save, Find and Print. Test: NP11 (`large`), saved files identical, also with `NP_W16FILES`. The USER side (3.1 copies for such counts, w16 does not) is noted in ARCHITECTURE 10.3 as not done: outside Notepad, no probe of the exact rule. | 7be0d37 |
| C2 | REAL (the 26K-32K range: FALSE) | Word Wrap copied the text into a new control in the same 64K segment, then destroyed the old one without checking. NP11's first run: w16's Notepad on 3.1's USER wrapping 51192 bytes showed "Not enough memory..." and was left empty (len 0; Save As then wrote a 0-byte file). A 30000-byte file wrapped and saved intact (both copies fit), so the loss needs about 32K or more, not 26K. 3.1's Notepad wraps 24000 bytes of 64-character lines and refuses 28000 and 51190 with IDS_WRAPLARGE, keeping the text. | The text's block is handed to the new control (EM_SETHANDLE; the new control's empty block back to the old one, which frees it); if the new control does not take it all, the old keeps it and IDS_WRAPLARGE. w16 wraps all it holds (ZOI: it already opens files 3.1 cannot). Test: NP11 (Word Wrap on and off on 51192 bytes, Find, saved files identical with 3.1's, which refuses). | 7be0d37 |

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | REAL (needs about 65410 characters) | find.c:49 `i + n <= len` in WORDs: with len near 0xFFFE and the caret within n of the end, i + n wraps; the loop then reads past the copy (past its NUL; a fault past the segment's limit) and, i wrapping to 0, could find a match before the caret. Reachable: the edit control's segment holds up to about 65450 characters, EM_LIMITTEXT 0 lets typing reach it, the Find text is up to 127 characters. | `n <= len - i` (and the Up search bounded by len). Not observable below that size: NP6 and NP11 unchanged. | 7be0d37 |
| H2 | REAL | No margin check against the page: Page Setup took top 6 / bottom 6 on an 11-inch page and Print then printed pages with no line, page after page (NP12's first run: 12688 bytes of blank pages from Generic / Text Only). 3.1 refuses such margins in Page Setup: "The Margin values are not correct..." when left + right or top + bottom reaches the page's width or height (8.4 and 10.9 taken, 8.5 and 11 not), focus back in Left; with no printer it takes any number (NP13). | `margins_fit` in Page Setup's OK (the printer's HORZRES/VERTRES over LOGPIXELS); at print time, margins that leave no line on the page (another printer chosen since) give the same message and nothing is printed. Tests: NP12, NP13 (logs identical). | 7be0d37 |
| H3 | REAL (by reading) | print.c:356 `int n = e - pos`: a line of 32768 characters or more made n negative, the line went to TabbedTextOut with a negative count (pos still advanced, so no loop: the line was lost). Not probed (a printer and a 32K line; the result follows from the code). | `fit()`: WORD counts, at most 32767 a call, the width found by doubling then halving (the extent asked for at most twice what fits, so its 16 bits do not wrap). | 7be0d37 |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | REAL | `LOWORD(...) > right - left`: a WORD against a negative int compares unsigned, so left + right wider than the page cut nothing (NP12's first run: left 5, right 5 accepted and printed). 3.1 refuses those margins in Page Setup (H2). | As H2; `fit()` returns 0 for a width of 0 or less. Test: NP12. | 7be0d37 |
| M2 | REAL (by reading) | Measured with GetTextExtent (a tab as one character), drawn with TabbedTextOut (tabs expanded): a line with tabs could run past the right margin. 3.1's printed page otherwise differs from w16's (UNVERIFIED in ARCHITECTURE 15.1; Generic / Text Only now shows it), so not compared. | GetTabbedTextExtent with the same default stops. | 7be0d37 |
| M3 | REAL (by reading; 3.1 not observable) | A failed NEWFRAME broke out and called ENDDOC, with no message. The SDK (EndDoc) says a job ended by an error is ended with AbortDoc; Notepad's own strings IDS_PRINTDISK and IDS_PRINTMEM exist for out-of-disk and out-of-memory. A full spool disk was not tried; a port that cannot be opened (C:\NODIR\NP.PRN) shows no message on 3.1 and none on w16. | ABORTDOC after a failed page; GDI's errors it has not reported itself (SP_NOTREPORTED, Petzold ch. 15): SP_OUTOFDISK IDS_PRINTDISK, SP_OUTOFMEMORY IDS_PRINTMEM, others IDS_CANTPRINT, the aborts nothing (UNVERIFIED on 3.1, marked). | 7be0d37 |
| M4 | HARDENING | `GlobalLock(dev_names)` fails only for a handle COMMDLG's PrintDlg did not give (it is moveable, not discardable). | NULL: as no printer. | 7be0d37 |
| M5 | REAL | WM_QUERYENDSESSION asked the changed-text question whatever was open, so with Save As up a Yes opened a second Save As. 3.1 (npqes: the message posted with SNAP's `postwin`; whole-screen captures): with Save As up, "You cannot quit Windows because the Save As dialog box in Notepad is open. Switch to Notepad, close this dialog box, and then try quitting Windows again." (string 48, OK, no icon) whether the text changed or not, and the box is system-modal (SNAP stopped); with Open, Find or Page Setup up or nothing, the changed-text question, also system-modal (no thick frame, as w16's then showed with one). | `file_query_end`: string 48 (w16's text says "w16") and FALSE while Save As is up, else the question; both MB_SYSTEMMODAL. Test: `tests/npqes.sh` (saveas and saved differ only in that word, 2737 pixels; none, open, find, setup, clean identical). | 7be0d37 |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | REAL (dialog order, by reading); FALSE (the blank name) | The STARTDOC error box came while the Printing dialog was still up (print.c:384 before 387). The name: "Cannot print the  file" for an untitled text cannot happen on 3.1 (Print is grayed without a printer, /P always names a file, and a port that cannot be opened gave no message on 3.1, pprbad probe); only w16's Ctrl+P (NotepadAccelerators=1) reaches it, where w16's own text is a choice, not a fidelity question. | The Printing dialog is destroyed and the window enabled before any message. The name kept. | 7be0d37 |
| L2 | HARDENING | `file[]` was used unset if DragQueryFile returned 0: only for a WM_DROPFILES not made by SHELL (a forged or empty HDROP); File Manager's drops name a file. | The name cleared first; nothing loaded without one. | 7be0d37 |
| L3 | FALSE | `s_cpy` writes `d[0]` for max <= 0, but no caller passes that: `set_title` (sizeof title - n, n < sizeof title), `make_filter` (96-byte buffer, short strings), `format_msg` (a + 1), `s_cat` (n < max checked), `expand_hf` (n[where] < STR_MAX * 2), `time_part`/`date_part` (4, 8), `file_load`/`file_save` (sizeof). | None. | - |

## Beyond the audit

- w16's USER takes GetWindowText's count and WM_GETTEXT's wParam as
  signed (C1); 3.1's copies for 51193. Recorded in ARCHITECTURE 10.3 as
  not done (USER work; needs a probe of the rule for 0x8000-0xFFFF and 0).
- w16's USER takes EM_SETSEL's positions as signed: a Find match at 51190
  selects the whole text there, so NP11 with `NP_W16FILES` fails (its
  last file is the typed "Q" alone) while it passes on 3.1's USER. Also
  recorded in ARCHITECTURE 10.3; not fixed here (USER, with its own
  probe and boot test). With `NP_W16FILES` the other scenarios run
  (files, find, wrap, undo) differ only by USER's own art and handles,
  their saved files identical.
- The printed page differs from 3.1's (header and text start lower on
  3.1, other line width) now that a printer driver can be used in the
  scenarios; recorded in ARCHITECTURE 15.1's UNVERIFIED list, not worked
  on here.
