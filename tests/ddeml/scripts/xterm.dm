; exploration (raw values, not compared): DdeUninitialize with a partner
; that never answers its TERMINATE: the termination dialog. Run with
; tests/ddeml/dmshot.sh and an input script (xterm.in: Close at once,
; xtermr.in: Retry, then Close): B's ticker logs the gaps the
; system-modal dialog makes (when it came and went), A how long
; DdeUninitialize took
raw on
start B
start C
C: rserve Svc Topic
init 0
conn Svc Topic
C: rnoterm 100
B: raw on
B: ticker
uninit
wins
quit B
quit C
