; exploration (not compared): DdeUninitialize with more answers coming
; back than a message queue holds
start B
init 0
B: init 0
B: ns Svc reg
conn Svc T1
conn Svc T2
conn Svc T3
conn Svc T4
conn Svc T5
conn Svc T6
conn Svc T7
conn Svc T8
conn Svc T9
conn Svc T10
conn Svc T11
uninit
B: uninit
quit B
