; exploration (not compared): which INITIATE sends a monitor reports,
; and when, with servers of its own library (D) and of another (B), a
; client of its own library (C)
startx M
start B
startx C
startx D
M: init 7F000001
B: init 0
C: init 0
D: init 0
B: ns Svc reg
D: ns Svc reg
C: conn Svc Topic
D: ret CONNECT 0
C: conn Svc Topic
C: connlist Svc Topic
B: ret CONNECT 0
C: conn Svc Topic
D: ret CONNECT 1
C: connlist Svc Topic
B: uninit
C: uninit
D: uninit
M: uninit
quit B
quit C
quit D
quit M
