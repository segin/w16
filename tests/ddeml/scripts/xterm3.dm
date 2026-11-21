; exploration (raw values, not compared): as xterm, the partner answering
; its fourth TERMINATE (with tests/ddeml/scripts/xtermc.in: Continue)
raw on
start B
start C
C: rserve Svc Topic
init 0
conn Svc Topic
C: rnoterm 3
B: raw on
B: ticker
uninit
wins
quit B
quit C
