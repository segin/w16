#!/bin/sh
# us43log.sh LOG: US43's log (tests/apps/us43.c) reduced to what the
# boot-test case alttab compares with Windows 3.1's: from BEGIN, the
# switching messages (WM_SYSCOMMAND and WH_CBT's HCBT_SYSCOMMAND and
# HCBT_ACTIVATE, WH_MSGFILTER's MSGF_NEXTWINDOW calls, WM_ACTIVATE,
# WM_ACTIVATEAPP, WM_NCACTIVATE, WM_QUERYOPEN), the keys and the focus of
# US43's own windows, the CoolSwitch lines. Left out: the system-modal
# box's phase (from its 's' to the next F12: MessageBox's own window and
# loop), the dialog manager's messages (its controls, its filter code 0,
# the focus while the dialog is up and as it ends),
# what is an address (WM_NCACTIVATE's lParam) or the keyboard's state in a
# dialog (bit 27 of a key's lParam there, 3.1's alone), the desktop's
# WM_PAINT, which comes through the loop when the host is slow, and 3.1's
# screen-saver check: a system timer of the switch window's (#32771, id
# FFFDh, every 10 s, in the shell's queue; US43 A is the shell) that comes
# through the loop as WM_SYSTIMER when its 10 s end while Alt is held in
# the shell's task (3.1: t=90626 ms, inside A's loop after the dialog's,
# once the input script's timing moved; w16's own run reaches that loop
# at another time)
sed -n '/^BEGIN/,$p' "$1" | tr -d '\r' |
    awk '/ CHAR 0073 /{skip=1} / CHAR 006D /{dlg=1} / KEYDOWN 007B /{skip=0; dlg=0}
         !skip && !(dlg && / (SET|KILL)FOCUS /)' |
    grep -E 'SYSCOMMAND|CBT ACTIVATE|FILTER 6|ACTIVATE|QUERYOPEN|SPI|WININICHANGE|destroys|^[A-E]s? US43[A-Za-z]* (SYS)?(KEYDOWN|KEYUP|SYSCHAR|SETFOCUS|KILLFOCUS)' |
    grep -v ' PAINT ' |
    grep -v 'FILTER 6 #32771 SYSTIMER FFFD ' |
    sed 's/\(NCACTIVATE 000[01]\) .*/\1/' |
    sed 's/\(FILTER 6 [^ ]* [A-Z]* [0-9A-F]* [0-9A-F]* [0-9A-F]\)8/\10/'
