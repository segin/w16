; exploration (not compared): the order DdeUninitialize terminates in
start B
start C
init 0
B: init 0
C: init 0
B: ns Svc reg
C: ns Svc2 reg
ns Self reg
conn Self S1
conn Svc T1
conn Svc2 T2
conn Svc T3
conn Svc2 T4
conn Svc T5
uninit
B: uninit
C: uninit
quit B
quit C
