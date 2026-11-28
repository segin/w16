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
# dialog (bit 27 of a key's lParam there, 3.1's alone), and the desktop's
# WM_PAINT, which comes through the loop when the host is slow
sed -n '/^BEGIN/,$p' "$1" | tr -d '\r' |
    awk '/ CHAR 0073 /{skip=1} / CHAR 006D /{dlg=1} / KEYDOWN 007B /{skip=0; dlg=0}
         !skip && !(dlg && / (SET|KILL)FOCUS /)' |
    grep -E 'SYSCOMMAND|CBT ACTIVATE|FILTER 6|ACTIVATE|QUERYOPEN|SPI|WININICHANGE|destroys|^[A-E]s? US43[A-Za-z]* (SYS)?(KEYDOWN|KEYUP|SYSCHAR|SETFOCUS|KILLFOCUS)' |
    grep -v ' PAINT ' |
    sed 's/\(NCACTIVATE 000[01]\) .*/\1/' |
    sed 's/\(FILTER 6 [^ ]* [A-Z]* [0-9A-F]* [0-9A-F]* [0-9A-F]\)8/\10/'
