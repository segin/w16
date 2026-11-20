; exploration (raw values, not compared): a list made with an old list
; that has conversations the new INITIATE does not find again, an empty
; list, and what DdeDisconnectList leaves
raw on
start B
start C
B: raw on
init 0
B: init 0
C: init 0
B: ns Svc reg
C: ns Svc2 reg
B: ret WILDCONNECT pairs:Svc/TopA,Svc/TopB
C: ret WILDCONNECT pairs:Svc2/TopC
connlist Svc 0
next l1
next l1 c.
next l1 c.
connlist 0 0 l1
wins
next l2
info c.
next l2 c.
info c.
next l2 c.
info c.
next l2 c.
disclist l2
wait 300
wins
next l2
lasterr
disclist l2
lasterr
connlist Nobody Topic
lasterr
next l3
wins
uninit
B: uninit
C: uninit
quit B
quit C
