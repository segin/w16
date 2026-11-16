; client transactions: every type, sync and async, the server's answers
; (ack, nack, busy, nothing, block), timeouts, abandon, user handles,
; reentrancy, invalid parameters
start B
init 0
B: init 0
B: ns Svc reg
conn Svc Topic
xact c1 request Item CF_TEXT - 2000
xact c1 request Item Private - 2000
xact c1 poke Item CF_TEXT text:pokedata 2000
xact c1 exec 0 CF_TEXT text:[cmd(1)] 2000
B: ret REQUEST 0
xact c1 request Item CF_TEXT - 2000
lasterr
B: ret POKE nack
xact c1 poke Item CF_TEXT text:x 2000
lasterr
B: ret POKE busy
xact c1 poke Item CF_TEXT text:x 2000
lasterr
B: ret POKE hex:80F7
xact c1 poke Item CF_TEXT text:x 2000
B: ret POKE hex:4155
xact c1 poke Item CF_TEXT text:x 2000
B: ret POKE hex:8012
xact c1 poke Item CF_TEXT text:x 2000
B: ret EXECUTE hex:0034
xact c1 exec 0 CF_TEXT text:[x] 2000
lasterr
B: ret EXECUTE busy
xact c1 exec 0 CF_TEXT text:[x] 2000
lasterr
B: ret EXECUTE ack
B: ret REQUEST item
mkdata d1 viahandle Item CF_TEXT
xact c1 poke Item CF_TEXT d1 2000
mkdata d2 wrongitem Other CF_TEXT
xact c1 poke Item CF_TEXT d2 2000
lasterr
mkdata d3 wrongfmt Item Private
xact c1 poke Item CF_TEXT d3 2000
lasterr
xact c1 request 0 CF_TEXT - 2000
lasterr
xact c1 exec Item CF_TEXT text:[withitem] 2000
lasterr
xact c1 exec 0 CF_TEXT - 2000
lasterr
xact c1 7 Item CF_TEXT - 2000
lasterr
xact 0 request Item CF_TEXT - 2000
lasterr
xact c1 request Item CF_TEXT - async
xact c1 poke Item CF_TEXT text:apoke async
xact c1 exec 0 CF_TEXT text:[async] async
xact c1 advstart Item CF_TEXT - async
xact c1 advstop Item CF_TEXT - async
wait 300
B: ret REQUEST block
xact c1 request Item CF_TEXT - 500
lasterr
B: info c1
B: enable c1 query
B: ret REQUEST item
B: enable c1 one
wait 300
B: ret REQUEST block
xact c1 request Item CF_TEXT - async
setuser c1 sync 1234
setuser c1 x6 5678
info c1 x6
setuser c1 99 5678
lasterr
abandon c1 99
lasterr
B: ret REQUEST item
B: enable c1 all
wait 300
xact c1 request Item CF_TEXT - async
abandon c1 0
lasterr
wait 300
B: ret REQUEST block
xact c1 request Item CF_TEXT - async
xact c1 request Item CF_TEXT - async
abandon 0 0
lasterr
B: ret REQUEST item
B: enable 0 all
wait 300
cbdo XACT_COMPLETE xact c1 request Item CF_TEXT - 2000
xact c1 request Item CF_TEXT - async
wait 300
lasterr
B: cbdo REQUEST enable c1 off
xact c1 request Item CF_TEXT - 2000
B: lasterr
B: enable c1 all
wait 300
B: cbdo REQUEST xact c1 request Item CF_TEXT - 2000
xact c1 request Item CF_TEXT - 2000
B: lasterr
disc c1
B: uninit
uninit
quit B
