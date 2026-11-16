; DdeInitialize: CBF_FAIL_* and CBF_SKIP_* filters, APPCMD_CLIENTONLY,
; APPCMD_FILTERINITS, re-initialising, bad arguments, monitor class
start B
init 0
B: init 0003F000
B: ns Svc reg
lasterr
conn Svc Topic
lasterr
B: ns 0 off
conn Svc Topic
lasterr
B: reinit 00030000
B: ns 0 off
conn Svc Topic
xact c1 request Item CF_TEXT - 2000
lasterr
xact c1 poke Item CF_TEXT text:x 2000
lasterr
xact c1 exec 0 CF_TEXT text:[x] 2000
lasterr
xact c1 advstart Item CF_TEXT - 2000
lasterr
xact c1 advstop Item CF_TEXT - 2000
lasterr
B: reinit 0000C000
xact c1 request Item CF_TEXT - 2000
xact c1 poke Item CF_TEXT text:x 2000
lasterr
xact c1 exec 0 CF_TEXT text:[x] 2000
lasterr
xact c1 advstart Item CF_TEXT - 2000
lasterr
B: reinit 00000000
B: ns 0 off
disc c1
B: reinit 003C0000
conn Svc Topic
disc c1
B: reinit 00040000
conn Svc Topic
inst 1
init 00080000
ns Reg reg
ns Reg unreg
uninit
inst 0
B: reinit 00000020
conn Other Topic
lasterr
B: reinit 00000000
conn Other Topic
B: uninit
inst 2
init 00000010
ns Cli reg
lasterr
conn Svc Topic
lasterr
uninit
inst 3
init 00000001
hsz Mon
ns Mon reg
lasterr
conn Svc Topic
lasterr
uninit
inst 2
init 00000002
lasterr
init 12345678
lasterr
inst 0
reinit 00000010
lasterr
reinit 00000001
lasterr
uninit
uninit
lasterr
quit B
