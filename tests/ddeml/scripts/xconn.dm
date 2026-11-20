; exploration (raw values, not compared): how DdeConnect and
; DdeConnectList send INITIATE (which windows see it), the windows and
; handles of a list with several servers, and the list's order
raw on
iwatch on
start B
start C
start D
B: raw on
C: raw on
B: iwatch on
C: iwatch on
D: iwatch on
init 0
B: init 0
C: init 0
B: ns Svc reg
C: ns Svc2 reg
wins
conn Svc2 Topic
B: ret WILDCONNECT pairs:Svc/TopA,Svc/TopB
C: ret WILDCONNECT pairs:Svc2/TopC
conn 0 Topic
connlist 0 0
wins
B: wins
next l1
info c.
next l1 c.
info c.
next l1 c.
info c.
next l1 c.
connlist 0 0 l1
wins
B: wins
next l2
info c.
next l2 c.
info c.
next l2 c.
info c.
next l2 c.
disclist l2
wait 500
uninit
B: uninit
C: uninit
quit B
quit C
quit D
