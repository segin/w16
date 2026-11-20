; exploration (not compared): the order in which DdeUninitialize frees
; the strings an instance still holds (a monitor's MH_CLEANUP events)
start M
start B
M: init 7F000001
init 0
B: init 0
B: hsz Z9
B: hsz Z8
hsz Z8
hsz Z9
hsz Z7
hsz Z6
hsz Z5
uninit
init 0
hsz Y1
hsz Y2
hsz Y3
hsz Y4
freehsz Y2
hsz Y5
keephsz Y1
keephsz Y3
hsz Y2
uninit
B: uninit
M: uninit
quit B
quit M
