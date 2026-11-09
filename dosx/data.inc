; data.inc - initialised data.

patch_tab       dw to_rm.seg, cb_c0.seg, 0

msg_nomem       db 13, 10, '  Insufficient extended memory to run Windows in standard mode.'
                db 13, 10, 13, 10, '  Increase the amount of available extended memory.'
                db 13, 10, '$'
; UNVERIFIED: Microsoft's texts for these three
msg_cpu         db 13, 10, '  Standard Mode: an 80286 or later processor is needed.', 13, 10, '$'
msg_v86         db 13, 10, '  Standard Mode: the processor is already in protected mode.', 13, 10, '$'
msg_nokrnl      db 13, 10, '  Standard Mode: cannot run KRNL286.EXE.', 13, 10, '$'
msg_clientfault db 13, 10, '  Standard Mode: Fault outside of MS-DOS Extender.', 13, 10, 0
msg_hostfault   db 13, 10, '  Standard Mode: Fault in MS-DOS Extender.', 13, 10, 0
; the register line (1 = the next value in fx_vals as 4 hex digits)
fx_names        db 'EC=', 1, ' CS=', 1, ' IP=', 1, ' AX=', 1, ' BX=', 1, ' CX=', 1
                db ' DX=', 1, 13, 10, 'SI=', 1, ' DI=', 1, ' BP=', 1, ' DS=', 1
                db ' ES=', 1, ' SS=', 1, ' SP=', 1, 13, 10, 0
s_krnl386       db 'KRNL386.EXE', 0
s_krnl286       db 'KRNL286.EXE', 0

optab           dw op_reflect, op_int31, op_int21, op_excdef, op_int10, op_int15
OP_COUNT        equ ($ - optab) / 2

; variables
imgseg          dw 0
psp_seg         dw 0
imglin          dd 0
cpu_type        db 0
is386           db 0
xms_ok          db 0
exit_code       db 0
client_active   db 0
x_kind          db 0
a_move          db 0
wp_used         db 0
xms             dd 0
xms_h           dw 0
emb_kb          dw 0
emb_lin         dd 0
emb_end         dd 0
ldt_lin         dd 0
pool_lin        dd 0
pool_last       dd 0
old2f           dd 0
old24           dd 0
kname           dw 0
exec_sp         dw 0
c_psp           dw 0
c_env           dw 0
c_ax            dw 0
c_bx            dw 0
c_cx            dw 0
c_dx            dw 0
c_si            dw 0
c_di            dw 0
c_bp            dw 0
c_ds            dw 0
c_ip            dw 0
c_cs            dw 0
c_ss            dw 0
c_sp            dw 0
c_fl            dw 0
c_dssel         dw 0
c_sssel         dw 0
c_envsel        dw 0
c_pspsel        dw 0
c_cssel         dw 0
dta_off         dw 0
dta_sel         dw 0
gdtr            dw 0, 0, 0
idtr            dw 0, 0, 0
rmidtr          dw 0, 0, 0
hs_top          dw 0
h3_top          dw 0
tb_top          dw 0
rmc_cur         dw 0
r286_sp         dw 0
x_ax            dw 0
x_fl            dw 0
x_idx           dw 0
x_i             dw 0
x_cs            dw 0
x_ss            dw 0
x_sp            dw 0
cb_off          dw 0
cb_seg          dw 0
cb_max          dw 0
cb_cap          dw 0
cb_depth        dw 0
cb_lin          dd 0
cb_newcap       dw 0
cb_newlin       dd 0
ldt_head        dw 0
seg_next        dw 0
a_hdr           dd 0
a_new           dd 0
a_dst           dd 0
lc_src          dd 0
lc_dst          dd 0
lc_n            dw 0
exit_ax         dw 0
pending_msg     dw 0
exc_sp          dw 0
rmx_depth       dw 0                    ; real-mode callbacks/hooks active
fx_vals:
fx_ec           dw 0
fx_cs           dw 0
fx_ip           dw 0
fx_ax           dw 0
fx_bx           dw 0
fx_cx           dw 0
fx_dx           dw 0
fx_si           dw 0
fx_di           dw 0
fx_bp           dw 0
fx_ds           dw 0
fx_es           dw 0
fx_ss           dw 0
fx_sp           dw 0
fcb_tb          dw 0
p29_src         dw 0
rw_buf          dw 0
rw_size         dw 0
rw_done         dw 0
rw_n            dw 0
io_n            dw 0
m48_hdr         dd 0
m48_sel         dw 0
p5f_si          dw 0
p5f_di          dw 0
p4b_blk         times 14 db 0
