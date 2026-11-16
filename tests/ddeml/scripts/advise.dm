; advise loops: hot, warm (NODATA), ACKREQ, several clients and formats,
; restarting a loop, the client's answers, late acks, stopping
start B
start C
init 0
B: init 0
B: ns Svc reg
C: init 0
conn Svc Topic
C: conn Svc Topic
xact c1 advstart Hot CF_TEXT - 2000
xact c1 advstart Warm CF_TEXT - 2000
xact c1 advstart+nodata Warm2 CF_TEXT - 2000
xact c1 advstart+ackreq Ack CF_TEXT - 2000
xact c1 advstart+ackreq+nodata AckWarm CF_TEXT - 2000
xact c1 advstart Hot Private - 2000
C: xact c1 advstart Hot CF_TEXT - 2000
info c1
B: postadv Topic Hot
B: postadv Topic Warm2
xact c1 request Warm2 CF_TEXT - 2000
B: postadv Topic Ack
B: postadv Topic AckWarm
B: postadv Topic Nothing
B: postadv 0 Hot
B: postadv Topic 0
B: postadv Other Hot
xact c1 advstart+nodata Hot CF_TEXT - 2000
B: postadv Topic Hot
xact c1 advstart Hot CF_TEXT - 2000
ret ADVDATA nack
B: postadv Topic Hot
ret ADVDATA busy
B: postadv Topic Hot
ret ADVDATA ack
ret ADVDATA block
B: postadv Topic Ack
B: postadv Topic Ack
B: postadv Topic Ack
wait 300
info c1
ret ADVDATA ack
enable c1 all
wait 500
B: ret ADVREQ 0
B: postadv Topic Hot
B: ret ADVREQ item
B: ret ADVSTART 0
xact c1 advstart Denied CF_TEXT - 2000
lasterr
B: ret ADVSTART 1
xact c1 advstop Never CF_TEXT - 2000
lasterr
xact c1 advstop Hot Private - 2000
xact c1 advstop Hot CF_TEXT - 2000
B: postadv Topic Hot
C: xact c1 advstop Hot CF_TEXT - 2000
xact c1 advstop Ack CF_TEXT - 2000
disc c1
B: postadv Topic Warm
C: disc c1
B: uninit
C: uninit
uninit
quit B
quit C
