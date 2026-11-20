; exploration (not compared): the order DdeUninitialize ends an
; instance's conversations with itself and with another instance of
; the same task in
init 0
ns Self reg
conn Self S1
conn Self S2
conn Self S3
inst 1
init 0
ns Oth reg
inst 0
conn Oth O1
conn Self S4
conn Oth O2
wins
uninit
inst 1
uninit
