; dpmibody.inc - DPMIPRB.COM's protected-mode part (tests/dos/dpmiprb.asm),
; assembled twice: BZ 16 (bits 16, a 16-bit client) and BZ 32 (bits 32, a
; 32-bit client in a 32-bit code segment over the same memory). Both use
; the 32-bit registers throughout; offsets stay below 64 KB, so the same
; data serves both.

%ifndef DPMIBODY_MACROS
%define DPMIBODY_MACROS
%macro LS 1+                    ; log a string
[section .data]
%%s:    db %1, 0
__SECT__
        push esi
        mov esi, %%s
        call L(puts)
        pop esi
%endmacro
%macro LH 2                     ; log a dword operand in %2 hex digits
        push eax
        push ecx
        mov eax, %1
        mov cl, %2
        call L(hex)
        pop ecx
        pop eax
%endmacro
%macro LR 3                     ; " name=" and a saved register
        LS %1
        LH dword [%2], %3
%endmacro
%macro LCF 0
        LS " cf="
        push eax
        movzx eax, byte [r_cf]
        LH eax, 1
        pop eax
%endmacro
%macro NL 0
        call L(nl)
%endmacro
%macro SAVE 0                   ; the registers after a call
        mov [r_eax], eax
        mov [r_ebx], ebx
        mov [r_ecx], ecx
        mov [r_edx], edx
        mov [r_esi], esi
        mov [r_edi], edi
        mov [r_es], es
        setc [r_cf]
%endmacro
%macro I31 1                    ; INT 31h function %1 (other registers set)
        mov ax, %1
        int 31h
        SAVE
%endmacro
%macro CLR 0                    ; all general registers 0
        xor eax, eax
        xor ebx, ebx
        xor ecx, ecx
        xor edx, edx
        xor esi, esi
        xor edi, edi
%endmacro
%endif

; an exception frame's i-th word (16-bit client) or dword (32-bit), with
; EBP pushed first
%if BZ = 16
%define FR(i) word [bp + 4 + 2 * (i)]
%macro GETF 2
        movzx %1, FR(%2)
%endmacro
%macro PUTF 2
        mov FR(%1), %2
%endmacro
%macro SETBP 0
        mov bp, sp
%endmacro
%else
%define FR(i) dword [ebp + 4 + 4 * (i)]
%macro GETF 2
        mov %1, FR(%2)
%endmacro
%macro PUTF 2
        mov FR(%1), %2
%endmacro
%macro SETBP 0
        mov ebp, esp
%endmacro
%endif

L(main):
        cld
        ; ---- the switch: what the host gave (logged now; real mode noted it) ----
        LS "DPMIPRB "
        movzx eax, byte [mode]
        LH eax, 2
        LS " bits="
        LH BZ, 2
        NL
        LS "1686 rm ax="
        LH dword [rm1686], 4
        NL
        LS "1687 ax="
        movzx eax, word [e_ax]
        LH eax, 4
        LS " bx="
        movzx eax, word [e_bx]
        LH eax, 4
        LS " cl="
        movzx eax, word [e_cx]
        LH eax, 2
        LS " dx="
        movzx eax, word [e_dx]
        LH eax, 4
        LS " si="
        movzx eax, word [e_si]
        LH eax, 4
        NL
        LS "INFO entry="
        LH dword [entry], 8
        NL
        LS "switch cs="
        movzx eax, word [sel_cs0]
        LH eax, 4
        LS " ds="
        movzx eax, word [sel_ds]
        LH eax, 4
        LS " ss="
        movzx eax, word [sel_ss0]
        LH eax, 4
        LS " es="
        movzx eax, word [sel_es0]
        LH eax, 4
        LS " fs="
        movzx eax, word [sel_fs0]
        LH eax, 4
        LS " gs="
        movzx eax, word [sel_gs0]
        LH eax, 4
%if BZ = 32
        LS " esphi="
        mov eax, [esp0]
        shr eax, 16
        LH eax, 4
%endif
        LS " fl="
        mov eax, [fl0]
        and eax, 0FFFFh & ~0010h & ~0004h       ; AF and PF: arithmetic left-overs
        LH eax, 4
        NL
        LS "desc cs"
        mov bx, [sel_cs0]
        call L(dsel)
        LS "desc ds"
        mov bx, [sel_ds]
        call L(dsel)
        LS "desc ss"
        mov bx, [sel_ss0]
        call L(dsel)
        LS "desc es"
        mov bx, [sel_es0]
        call L(dsel)
        LS "env sel="
        movzx eax, word [2Ch]
        LH eax, 4
        LS " desc"
        mov bx, [2Ch]
        call L(dsel)
%if BZ = 32
        LS "desc c32"
        mov bx, [sel_c32]
        call L(dsel)
%endif

        cmp byte [mode], 'C'
        jne .notc
        jmp L(conc)
.notc:
        ; ---- INT 2Fh in protected mode ----
        mov ax, 1686h
        int 2Fh
        SAVE
        LS "2F 1686"
        LR " ax=", r_eax, 4
        NL
        mov ax, 1600h
        int 2Fh
        SAVE
        LS "2F 1600"
        LR " ax=", r_eax, 4
        NL
        mov ax, 1683h
        xor bx, bx
        int 2Fh
        SAVE
        LS "2F 1683"
        LR " ax=", r_eax, 4
        LR " bx=", r_ebx, 4
        NL
        push es
        mov ax, 168Ah
        mov esi, msdos
        xor edi, edi
        int 2Fh
        SAVE
        pop es
        LS "2F 168A"
        LR " al=", r_eax, 2
        NL
        LS "INFO 168A es:di="
        movzx eax, word [r_es]
        LH eax, 4
        LR ":", r_edi, 8
        NL
        mov ax, 1687h
        int 2Fh
        SAVE
        LS "2F 1687 pm"
        LR " ax=", r_eax, 4
        NL

        ; ---- version ----
        CLR
        I31 0400h
        LS "0400"
        LCF
        LR " ax=", r_eax, 4
        LR " bx=", r_ebx, 4
        LR " cl=", r_ecx, 2
        LR " dx=", r_edx, 4
        NL

        ; ---- LDT descriptors ----
        CLR
        mov cx, 3
        I31 0000h
        LS "0000 cx=3"
        LCF
        LR " ax=", r_eax, 4
        NL
        mov ax, [r_eax]
        mov [tmp], ax           ; sel0, sel0+8, sel0+16
        CLR
        I31 0003h
        LS "0003"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        mov bx, [tmp]
        mov cx, 1
        mov dx, 2345h
        I31 0007h
        LS "0007"
        LCF
        NL
        CLR
        mov bx, [tmp]
        I31 0006h
        LS "0006"
        LCF
        LR " cx=", r_ecx, 4
        LR " dx=", r_edx, 4
        NL
        CLR
        mov bx, [tmp]
        mov dx, 0FFFFh
        I31 0008h
        LS "0008 ffff"
        LCF
        NL
        CLR
        mov bx, [tmp]
        mov cx, 0F2h
        I31 0009h
        LS "0009 f2"
        LCF
        mov bx, [tmp]
        call L(dsel)
        CLR
        mov bx, [tmp]
        mov cx, 10h
        I31 0008h
        LS "0008 100000"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        mov bx, [tmp]
        mov cx, 1Fh
        mov dx, 0FFFFh
        I31 0008h
        LS "0008 1fffff"
        LCF
        mov bx, [tmp]
        call L(dsel)
        CLR
        mov bx, [tmp]
        mov cx, 92h
        I31 0009h
        LS "0009 92"
        LCF
        mov bx, [tmp]
        call L(dsel)
        CLR
        mov bx, [tmp]
        mov cx, 40F2h
        I31 0009h
        LS "0009 40f2"
        LCF
        mov bx, [tmp]
        call L(dsel)
        CLR
        mov bx, cs
        I31 000Ah
        LS "000A cs"
        LCF
        LR " ax=", r_eax, 4
        mov bx, [r_eax]
        call L(dsel)
        CLR
        mov bx, [r_eax]
        I31 0001h
        ; 000Ch: a DPL 0 data descriptor, then a DPL 3 one
        mov dword [desc], 0000FFFFh
        mov dword [desc + 4], 00009200h
        CLR
        push es
        push ds
        pop es
        mov bx, [tmp]
        add bx, 8
        mov edi, desc
        I31 000Ch
        pop es
        LS "000C 92"
        LCF
        NL
        mov dword [desc], 1000FFFFh
        mov dword [desc + 4], 0000F201h
        CLR
        push es
        push ds
        pop es
        mov bx, [tmp]
        add bx, 8
        mov edi, desc
        I31 000Ch
        pop es
        LS "000C f2"
        LCF
        mov bx, [tmp]
        add bx, 8
        call L(dsel)
        CLR
        mov bx, [tmp]
        add bx, 16
        I31 0001h
        LS "0001"
        LCF
        NL
        CLR
        mov bx, [tmp]
        add bx, 16
        I31 0001h
        LS "0001 again"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        push es
        push ds
        pop es
        mov bx, [tmp]
        add bx, 16
        mov edi, desc
        I31 000Bh
        pop es
        LS "000B freed"
        LCF
        NL
        CLR
        mov bx, 0B800h
        I31 0002h
        LS "0002 b800"
        LCF
        LR " ax=", r_eax, 4
        mov ax, [r_eax]
        mov [tmp + 4], ax
        mov bx, ax
        call L(dsel)
        CLR
        mov bx, 0B800h
        I31 0002h
        LS "0002 again"
        LCF
        mov ax, [r_eax]
        cmp ax, [tmp + 4]
        sete al
        movzx eax, al
        LS " same="
        LH eax, 1
        NL
        CLR
        mov bx, 40h
        I31 0002h
        mov ax, [r_eax]
        mov [sel_40], ax
        CLR
        mov bx, 004Ch
        I31 000Dh
        LS "000D 4c"
        LCF
        LR " ax=", r_eax, 4
        NL
        cmp byte [r_cf], 0
        jne .no0d
        CLR
        mov bx, 004Ch
        I31 0001h
.no0d:
        CLR
        I31 0000h
        LS "0000 cx=0"
        LCF
        LR " ax=", r_eax, 4
        NL
        cmp byte [r_cf], 0
        jne .no00
        mov bx, [r_eax]
        CLR
        mov bx, [r_eax]
        I31 0001h
.no00:
        CLR
        mov bx, [tmp]
        I31 0001h
        CLR
        mov bx, [tmp]
        add bx, 8
        I31 0001h
        CLR
        mov bx, 0FFF4h
        I31 0006h
        LS "0006 fff4"
        LCF
        LR " ax=", r_eax, 4
        NL

        ; ---- DOS memory ----
        CLR
        mov bx, 100h
        I31 0100h
        LS "0100 100"
        LCF
        mov ax, [r_eax]
        sub ax, [psp]
        movzx eax, ax
        LS " seg-psp="
        LH eax, 4
        LR " dx=", r_edx, 4
        mov ax, [r_edx]
        mov [tmp], ax
        mov bx, ax
        call L(dsel)
        CLR
        mov dx, [tmp]
        mov bx, 200h
        I31 0102h
        LS "0102 200"
        LCF
        NL
        LS "INFO 0102"
        mov bx, [tmp]
        call L(dsel)
        CLR
        mov dx, [tmp]
        I31 0101h
        LS "0101"
        LCF
        NL
        CLR
        mov bx, 0FFFFh
        I31 0100h
        LS "0100 ffff"
        LCF
        LR " ax=", r_eax, 4
        NL
        LS "INFO 0100 largest bx="
        LH dword [r_ebx], 4
        NL
        CLR
        I31 0101h
        LS "0101 0"
        LCF
        LR " ax=", r_eax, 4
        NL

        ; ---- interrupt vectors ----
        CLR
        mov bl, 21h
        I31 0200h
        LS "INFO 0200 21="
        LR "", r_ecx, 4
        LR ":", r_edx, 4
        NL
        CLR
        mov bl, 62h
        I31 0200h
        mov ax, [r_ecx]
        mov [oldv62 + 2], ax
        mov ax, [r_edx]
        mov [oldv62], ax
        CLR
        mov bl, 62h
        mov cx, [psp]
        mov dx, rm_int62
        I31 0201h
        LS "0201 62"
        LCF
        CLR
        mov bl, 62h
        I31 0200h
        mov ax, [r_ecx]
        cmp ax, [psp]
        sete al
        movzx eax, al
        LS " back="
        LH eax, 1
        LR ":", r_edx, 4
        NL
        mov ebx, 21h
        call L(pmvec)
        mov ebx, 8
        call L(pmvec)
        mov ebx, 2Fh
        call L(pmvec)
        mov ebx, 31h
        call L(pmvec)
        CLR
        mov bl, 61h
        mov cx, cs
        mov edx, L(int61)
        I31 0205h
        LS "0205 61"
        LCF
        mov eax, 1234h
        int 61h
        LS " int61="
        LH dword [int61cnt], 2
        CLR
        mov bl, 61h
        I31 0204h
        mov ax, [r_ecx]
        mov bx, cs
        cmp ax, bx
        sete al
        movzx eax, al
        LS " back="
        LH eax, 1
        mov eax, [r_edx]
        cmp eax, L(int61)
        sete al
        movzx eax, al
        LH eax, 1
        NL
        ; reflection: INT 62h has no protected-mode handler
        mov eax, 7000h
        int 62h
        mov [tmp], eax
        LS "int62 refl count="
        movzx eax, word [rm62cnt]
        LH eax, 2
        LS " ax="
        LH dword [tmp], 4
        NL
        LS "INFO int62 rm ds="
        movzx eax, word [rm62ds]
        LH eax, 4
        NL
        ; INT 21h AH=30h, and a selector in DS through the reflection
        mov ax, 3000h
        int 21h
        SAVE
        LS "int21 30"
        LCF
        LR " ax=", r_eax, 4
        NL

        ; ---- exceptions ----
        mov ebx, 0
        call L(getex)
        mov ebx, 3
        call L(getex)
        mov ebx, 6
        call L(getex)
        mov ebx, 0Dh
        call L(getex)
        LS "INFO 0202 0d="
        movzx eax, word [oldex + 3 * 8 + 4]
        LH eax, 4
        LS ":"
        LH dword [oldex + 3 * 8], 8
        NL
        ; #GP: a selector past the LDT's descriptors
        mov ebx, 0Dh
        mov edx, L(ex0d)
        call L(setex)
        mov dword [exc_resume], .gpres
        mov dword [exc_no], 0FFh
        mov ax, [badsel]
.gpip:  mov es, ax
.gpres: push ds
        pop es
        mov ebx, .gpip
        call L(exlog)
        ; #DE
        mov ebx, 0
        mov edx, L(ex00)
        call L(setex)
        mov dword [exc_resume], .deres
        mov dword [exc_no], 0FFh
        xor edx, edx
        xor ecx, ecx
        mov eax, 1
.deip:  div ecx
.deres: mov ebx, .deip
        call L(exlog)
        ; #UD
        mov ebx, 6
        mov edx, L(ex06)
        call L(setex)
        mov dword [exc_resume], .udres
        mov dword [exc_no], 0FFh
.udip:  ud2
.udres: mov ebx, .udip
        call L(exlog)
        ; INT 3: an exception handler and a protected-mode vector both set
        mov ebx, 3
        mov edx, L(ex03)
        call L(setex)
        CLR
        mov bl, 3
        I31 0204h
        mov ax, [r_ecx]
        mov [tmp + 8], ax
        mov eax, [r_edx]
        mov [tmp + 4], eax
        CLR
        mov bl, 3
        mov cx, cs
        mov edx, L(int3)
        I31 0205h
        mov dword [exc_resume], .i3res
        mov dword [exc_no], 0FFh
        int3
.i3res: LS "int3 exc="
        LH dword [exc_no], 2
        LS " pmvec="
        LH dword [int3pm], 2
        NL
        CLR
        mov bl, 3
        mov cx, [tmp + 8]
        mov edx, [tmp + 4]
        I31 0205h
        ; the old handlers back
        mov ebx, 0
        call L(resex)
        mov ebx, 3
        call L(resex)
        mov ebx, 6
        call L(resex)
        mov ebx, 0Dh
        call L(resex)
        CLR
        mov bl, 20h
        I31 0202h
        LS "0202 20"
        LCF
        LR " ax=", r_eax, 4
        NL

        ; ---- the timer's interrupt in protected mode ----
        CLR
        mov bl, 8
        I31 0204h
%if BZ = 16
        mov ax, [r_edx]
        mov [old8], ax
        mov ax, [r_ecx]
        mov [old8 + 2], ax
%else
        mov eax, [r_edx]
        mov [old8], eax
        mov ax, [r_ecx]
        mov [old8 + 4], ax
%endif
        CLR
        mov bl, 8
        mov cx, cs
        mov edx, L(int8)
        I31 0205h
        mov dword [tick_pm], 0
        sti
        push es
        mov es, [sel_40]
        mov bx, [es:6Ch]
        mov ecx, 4000000h
.tw:    mov ax, [es:6Ch]
        sub ax, bx
        cmp ax, 4
        jae .td
        dec ecx
        jnz .tw
.td:    pop es
        mov eax, [tick_pm]
        test eax, eax
        setnz al
        movzx eax, al
        LS "irq0 in pm="
        LH eax, 1
        ; ticks while V86 code runs (0301h to a loop)
        mov dword [tick_pm], 0
        call L(clrr)
        mov ax, [psp]
        mov [rmr + 2Ch], ax
        mov word [rmr + 2Ah], rm_wait
        push es
        push ds
        pop es
        CLR
        mov edi, rmr
        I31 0301h
        pop es
        mov eax, [tick_pm]
        test eax, eax
        setnz al
        movzx eax, al
        LS " in v86="
        LH eax, 1
        NL
        CLR
        mov bl, 8
%if BZ = 16
        mov dx, [old8]
        mov cx, [old8 + 2]
%else
        mov edx, [old8]
        mov cx, [old8 + 4]
%endif
        I31 0205h

        ; ---- translation services ----
        call L(clrr)
        mov dword [rmr + 1Ch], 3000h
        push es
        push ds
        pop es
        CLR
        mov bx, 21h
        mov edi, rmr
        I31 0300h
        pop es
        LS "0300 21/30"
        LCF
        LS " ax="
        LH dword [rmr + 1Ch], 4
        NL
        ; 0301h with two words copied from the stack
        call L(clrr)
        mov ax, [psp]
        mov [rmr + 2Ch], ax
        mov word [rmr + 2Ah], rm_args
        push es
        push ds
        pop es
        push word 2222h
        push word 1111h
        CLR
        mov cx, 2
        mov edi, rmr
        I31 0301h
        add esp, 4
        pop es
        LS "0301 args"
        LCF
        LS " ax="
        LH dword [rmr + 1Ch], 4
        LS " bx="
        LH dword [rmr + 10h], 4
        NL
        call L(clrr)
        mov ax, [psp]
        mov [rmr + 2Ch], ax
        mov word [rmr + 2Ah], rm_iret
        push es
        push ds
        pop es
        CLR
        mov edi, rmr
        I31 0302h
        pop es
        LS "0302"
        LCF
        LS " ax="
        LH dword [rmr + 1Ch], 4
        NL

        ; real-mode callbacks
        call L(clrr)
        push ds
        push es
        push ds
        pop es
        mov ax, cs
        mov ds, ax
        CLR
        mov esi, L(cbproc)
        mov edi, cbr
        mov ax, 0303h
        int 31h
        pop es
        pop ds
        SAVE
        LS "0303"
        LCF
        NL
        LS "INFO 0303 cx:dx="
        LH dword [r_ecx], 4
        LR ":", r_edx, 4
        NL
        mov ax, [r_edx]
        mov [cbaddr], ax
        mov ax, [r_ecx]
        mov [cbaddr + 2], ax
        ; real-mode code calls it (rm_cb2), through 0301h
        call L(clrr)
        mov ax, [psp]
        mov [rmr + 2Ch], ax
        mov word [rmr + 2Ah], rm_cb2
        push es
        push ds
        pop es
        CLR
        mov edi, rmr
        I31 0301h
        pop es
        LS "callback"
        LCF
        LS " ax="
        LH dword [rmr + 1Ch], 4
        LS " ran="
        LH dword [cb_cnt], 2
        LS " param="
        LH dword [tmp + 12], 4
        mov ax, [cb_es]
        cmp ax, [sel_ds]
        sete al
        movzx eax, al
        LS " es="
        LH eax, 1
        mov eax, [cb_edi]
        cmp eax, cbr
        sete al
        movzx eax, al
        LS " di="
        LH eax, 1
        NL
        CLR
        mov cx, [cbaddr + 2]
        mov dx, [cbaddr]
        I31 0304h
        LS "0304"
        LCF
        NL
        CLR
        mov cx, [cbaddr + 2]
        mov dx, [cbaddr]
        I31 0304h
        LS "0304 again"
        LCF
        LR " ax=", r_eax, 4
        NL
        ; how many callbacks there are
        xor ebp, ebp
.cbn:   push ds
        push es
        push ds
        pop es
        mov ax, cs
        mov ds, ax
        CLR
        mov esi, L(cbproc)
        mov edi, cbr
        mov ax, 0303h
        int 31h
        pop es
        pop ds
        jc .cbe
        mov [cbh + ebp * 4], dx
        mov [cbh + ebp * 4 + 2], cx
        inc ebp
        cmp ebp, 1024
        jb .cbn
.cbe:   LS "0303 count="
        LH ebp, 4
        NL
.cbf:   test ebp, ebp
        jz .cbg
        dec ebp
        CLR
        mov dx, [cbh + ebp * 4]
        mov cx, [cbh + ebp * 4 + 2]
        mov ax, 0304h
        int 31h
        jmp .cbf
.cbg:
        ; state save and raw switches
        CLR
        I31 0305h
        LS "0305"
        LCF
        LR " ax=", r_eax, 4
        NL
        LS "INFO 0305 rm="
        LH dword [r_ebx], 4
        LR ":", r_ecx, 4
        LR " pm=", r_esi, 4
        LR ":", r_edi, 8
        NL
        CLR
        I31 0306h
        LS "0306"
        LCF
        NL
        LS "INFO 0306 rm2pm="
        LH dword [r_ebx], 4
        LR ":", r_ecx, 4
        LR " pm2rm=", r_esi, 4
        LR ":", r_edi, 8
        NL
        mov ax, [r_ecx]
        mov [rm2pm], ax
        mov ax, [r_ebx]
        mov [rm2pm + 2], ax
%if BZ = 16
        mov ax, [r_edi]
        mov [pm2rm], ax
        mov ax, [r_esi]
        mov [pm2rm + 2], ax
%else
        mov eax, [r_edi]
        mov [pm2rm], eax
        mov ax, [r_esi]
        mov [pm2rm + 4], ax
%endif
        ; to real mode and back: rm_raw runs INT 21h 30h there
        mov word [raw_ver], 0
        mov [raw_back_cs], cs
        mov dword [raw_back_ip], L(rawback)
        mov [raw_save_esp], esp
        mov [raw_back_sp], esp
        mov ax, [psp]
        mov cx, ax
        mov dx, ax
        mov si, ax
        mov ebx, esp
        sub ebx, 256
        movzx ebx, bx
        mov edi, rm_raw
        jmp far [pm2rm]
L(rawback):
        mov esp, [raw_save_esp]
        mov ax, ds
        cmp ax, [sel_ds]
        sete al
        movzx eax, al
        LS "raw ds="
        LH eax, 1
        mov ax, ss
        cmp ax, [sel_ss0]
        sete al
        movzx eax, al
        LS " ss="
        LH eax, 1
        LS " ver="
        movzx eax, word [raw_ver]
        LH eax, 4
        mov ax, [raw_ss]
        cmp ax, [psp]
        sete al
        movzx eax, al
        LS " rmss="
        LH eax, 1
        NL

        ; ---- memory ----
        CLR
        push es
        push ds
        pop es
        mov edi, st_buf
        I31 0500h
        pop es
        LS "0500"
        LCF
        NL
        xor ebx, ebx
.mi:    LS "MEM 0500 "
        LH ebx, 1
        LS "="
        LH dword [st_buf + ebx * 4], 8
        NL
        inc ebx
        cmp ebx, 12
        jb .mi
        CLR
        mov cx, 0
        mov bx, 1
        I31 0501h
        LS "0501 64k"
        LCF
        NL
        LS "INFO 0501 lin="
        LH dword [r_ebx], 4
        LH dword [r_ecx], 4
        LR " handle=", r_esi, 4
        LH dword [r_edi], 4
        NL
        mov ax, [r_ebx]
        shl eax, 16
        mov ax, [r_ecx]
        mov [tmp], eax          ; lin
        mov ax, [r_esi]
        shl eax, 16
        mov ax, [r_edi]
        mov [tmp + 4], eax      ; handle
        CLR
        mov cx, 1
        I31 0000h
        mov ax, [r_eax]
        mov [tmp + 8], ax       ; a selector for it
        CLR
        mov bx, [tmp + 8]
        mov cx, [tmp + 2]
        mov dx, [tmp]
        I31 0007h
        CLR
        mov bx, [tmp + 8]
        mov dx, 0FFFFh
        I31 0008h
        push es
        mov es, [tmp + 8]
        mov dword [es:0], 0CAFE0001h
        mov dword [es:0FFFCh], 0CAFE0002h
        mov eax, [es:100h]
        mov [tmp + 12], eax
        pop es
        LS "INFO 0501 data first="
        LH dword [tmp + 12], 8
        NL
        CLR
        mov bx, 2
        mov si, [tmp + 6]
        mov di, [tmp + 4]
        I31 0503h
        LS "0503 128k"
        LCF
        mov ax, [r_ebx]
        shl eax, 16
        mov ax, [r_ecx]
        cmp eax, [tmp]
        sete al
        movzx eax, al
        LS " samelin="
        LH eax, 1
        mov ax, [r_esi]
        shl eax, 16
        mov ax, [r_edi]
        cmp eax, [tmp + 4]
        sete al
        movzx eax, al
        LS " samehandle="
        LH eax, 1
        mov ax, [r_ebx]
        shl eax, 16
        mov ax, [r_ecx]
        mov [tmp], eax
        mov ax, [r_esi]
        shl eax, 16
        mov ax, [r_edi]
        mov [tmp + 4], eax
        CLR
        mov bx, [tmp + 8]
        mov cx, [tmp + 2]
        mov dx, [tmp]
        I31 0007h
        push es
        mov es, [tmp + 8]
        mov eax, [es:0]
        cmp eax, 0CAFE0001h
        sete al
        mov ebx, [es:0FFFCh]
        cmp ebx, 0CAFE0002h
        sete ah
        and al, ah
        movzx eax, al
        pop es
        LS " kept="
        LH eax, 1
        NL
        ; lock, unlock, the real-mode region, page size, paging hints
        CLR
        mov bx, [tmp + 2]
        mov cx, [tmp]
        mov di, 1000h
        I31 0600h
        LS "0600"
        LCF
        CLR
        mov bx, [tmp + 2]
        mov cx, [tmp]
        mov di, 1000h
        I31 0601h
        LS " 0601"
        LCF
        movzx eax, word [psp]
        shl eax, 4
        mov [tmp + 12], eax
        CLR
        mov bx, [tmp + 14]
        mov cx, [tmp + 12]
        mov di, 1000h
        I31 0602h
        LS " 0602"
        LCF
        CLR
        mov bx, [tmp + 14]
        mov cx, [tmp + 12]
        mov di, 1000h
        I31 0603h
        LS " 0603"
        LCF
        CLR
        I31 0604h
        LS " 0604"
        LCF
        LR " bx=", r_ebx, 4
        LR " cx=", r_ecx, 4
        NL
        CLR
        mov bx, [tmp + 2]
        mov cx, [tmp]
        mov di, 1000h
        I31 0702h
        LS "0702"
        LCF
        CLR
        mov bx, [tmp + 2]
        mov cx, [tmp]
        mov di, 1000h
        I31 0703h
        LS " 0703"
        LCF
        NL
        CLR
        mov si, [tmp + 6]
        mov di, [tmp + 4]
        I31 0502h
        LS "0502"
        LCF
        NL
        CLR
        mov si, [tmp + 6]
        mov di, [tmp + 4]
        I31 0502h
        LS "0502 again"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        I31 0501h
        LS "0501 0"
        LCF
        LR " ax=", r_eax, 4
        NL
        ; as many 64 KB blocks as there are (up to 32 MB)
        xor ebp, ebp
.ma:    CLR
        mov bx, 1
        mov ax, 0501h
        int 31h
        jc .me
        mov [mh + ebp * 4], di
        mov [mh + ebp * 4 + 2], si
        inc ebp
        cmp ebp, 512
        jb .ma
.me:    SAVE
        LS "MEM 0501 blocks="
        LH ebp, 4
        LCF
        LR " ax=", r_eax, 4
        NL
        mov [tmp + 12], ebp
.mf:    test ebp, ebp
        jz .mg
        dec ebp
        CLR
        mov di, [mh + ebp * 4]
        mov si, [mh + ebp * 4 + 2]
        mov ax, 0502h
        int 31h
        jmp .mf
.mg:    push es
        push ds
        pop es
        CLR
        mov edi, desc + 0
        mov edi, st_buf + 256
        I31 0500h
        pop es
        mov eax, [st_buf + 256]
        cmp eax, [st_buf]
        sete al
        movzx eax, al
        LS "0500 after same="
        LH eax, 1
        NL
        ; one block of the largest size 0500h gave
        CLR
        mov bx, [st_buf + 2]
        mov cx, [st_buf]
        I31 0501h
        LS "0501 largest"
        LCF
        LR " ax=", r_eax, 4
        NL
        cmp byte [r_cf], 0
        jne .nolg
        CLR
        mov si, [r_esi]
        mov di, [r_edi]
        I31 0502h
.nolg:
        CLR
        mov bx, [tmp + 8]
        I31 0001h

        ; ---- physical mapping, virtual interrupts, vendor, debug, unknown ----
        CLR
        mov bx, 0Ah
        mov di, 1
        I31 0800h
        LS "0800 a0000"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        mov bx, 0FE00h
        mov di, 1000h
        I31 0800h
        LS "0800 fe000000"
        LCF
        NL
        cli
        CLR
        I31 0902h
        LS "0902 cli"
        LCF
        LR " al=", r_eax, 2
        CLR
        I31 0901h
        LS " 0901"
        LCF
        LR " al=", r_eax, 2
        CLR
        I31 0900h
        LS " 0900"
        LCF
        LR " al=", r_eax, 2
        CLR
        I31 0902h
        LS " 0902"
        LCF
        LR " al=", r_eax, 2
        pushfd
        pop eax
        shr eax, 9
        and eax, 1
        LS " if="
        LH eax, 1
        sti
        NL
        push es
        CLR
        mov esi, msdos
        I31 0A00h
        pop es
        LS "0A00"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        mov bx, [tmp + 2]
        mov cx, [tmp]
        mov dx, 0104h
        I31 0B00h
        LS "0B00"
        LCF
        LR " ax=", r_eax, 4
        NL
        cmp byte [r_cf], 0
        jne .nob
        mov bx, [r_ebx]
        CLR
        mov bx, [r_ebx]
        I31 0B01h
.nob:
        CLR
        I31 0E00h
        LS "0E00"
        LCF
        LR " ax=", r_eax, 4
        NL
        CLR
        I31 0401h
        LS "0401"
        LCF
        LR " ax=", r_eax, 4
        NL

        ; ---- INT 21h in protected mode with pointers ----
        mov ah, 62h
        int 21h
        SAVE
        LS "int21 62"
        LCF
        LR " bx=", r_ebx, 4
        NL
        push es
        mov ah, 2Fh
        int 21h
        SAVE
        pop es
        LS "int21 2f"
        LCF
        movzx eax, word [r_es]
        LS " es="
        LH eax, 4
        LR " bx=", r_ebx, 4
        NL
        mov ah, 3Ch
        xor ecx, ecx
        mov edx, tmpname
        int 21h
        SAVE
        LS "int21 3c"
        LCF
        NL
        cmp byte [r_cf], 0
        jne .nof
        mov ebx, [r_eax]
        push ebx
        mov ah, 40h
        mov ecx, 5
        mov edx, msdos
        int 21h
        SAVE
        LS "int21 40"
        LCF
        LR " ax=", r_eax, 4
        NL
        pop ebx
        mov ah, 3Eh
        int 21h
        mov ah, 41h
        mov edx, tmpname
        int 21h
        SAVE
        LS "int21 41"
        LCF
        NL
.nof:   mov ah, 48h
        mov bx, 10h
        int 21h
        SAVE
        LS "int21 48"
        LCF
        LR " ax=", r_eax, 4
        NL
        cmp byte [r_cf], 0
        jne .no48
        push es
        mov es, [r_eax]
        mov ah, 49h
        int 21h
        SAVE
        pop es
        LS "int21 49"
        LCF
        NL
.no48:  push es
        mov ax, 3521h
        int 21h
        SAVE
        pop es
        LS "INFO int21 35="
        movzx eax, word [r_es]
        LH eax, 4
        LR ":", r_ebx, 8
        NL

        ; ---- a nested client: DPMIPRB 32 <tag+1> from protected mode ----
        mov ebx, childtail
        cmp byte [mode], 'O'
        je .nest
        mov ebx, childt16
        cmp byte [mode], 'N'
        jne .non
.nest:  mov al, [tag]
        inc al
        mov [ebx + 5], al
        mov [ptab + 2], bx
        mov ax, [envseg]
        mov [ptab], ax
        mov ax, [psp]
        mov [ptab + 4], ax
        mov word [ptab + 6], 5Ch
        mov [ptab + 8], ax
        mov word [ptab + 10], 6Ch
        mov [ptab + 12], ax
        call L(clrr)
        mov dword [rmr + 1Ch], 4B00h
        mov dword [rmr + 14h], childname
        mov dword [rmr + 10h], ptab
        mov ax, [psp]
        mov [rmr + 24h], ax
        mov [rmr + 22h], ax
        push es
        push ds
        pop es
        CLR
        mov bx, 21h
        mov edi, rmr
        I31 0300h
        pop es
        movzx eax, word [rmr + 20h]
        and eax, 1
        LS "exec child cf="
        LH eax, 1
        call L(clrr)
        mov dword [rmr + 1Ch], 4D00h
        push es
        push ds
        pop es
        CLR
        mov bx, 21h
        mov edi, rmr
        I31 0300h
        pop es
        LS " code="
        LH dword [rmr + 1Ch], 4
        NL
        ; still ours: the version, DS's base, INT 61h's handler
        CLR
        I31 0400h
        LS "after child 0400"
        LCF
        LR " ax=", r_eax, 4
        CLR
        mov bx, ds
        I31 0006h
        mov ax, [r_ecx]
        shl eax, 16
        mov ax, [r_edx]
        movzx ebx, word [psp]
        shl ebx, 4
        cmp eax, ebx
        sete al
        movzx eax, al
        LS " ds="
        LH eax, 1
        mov dword [int61cnt], 0
        int 61h
        LS " int61="
        LH dword [int61cnt], 2
        NL
.non:
        CLR
        mov bl, 62h
        mov cx, [oldv62 + 2]
        mov dx, [oldv62]
        I31 0201h               ; the box's vector back
        cmp byte [mode], 'G'
        jne L(fin)
        LS "G fault"
        NL
        mov ax, [badsel]
        mov es, ax
        LS "G survived"
        NL
        jmp L(fin)

; ---- two boxes at once ----
L(conc):
        CLR
        mov bx, 4               ; 256 KB
        I31 0501h
        LS "C 0501"
        LCF
        NL
        cmp byte [r_cf], 0
        jne L(fin)
        mov ax, [r_ebx]
        shl eax, 16
        mov ax, [r_ecx]
        mov [tmp], eax
        CLR
        mov cx, 1
        I31 0000h
        mov ax, [r_eax]
        mov [tmp + 8], ax
        CLR
        mov bx, [tmp + 8]
        mov cx, [tmp + 2]
        mov dx, [tmp]
        I31 0007h
        CLR
        mov bx, [tmp + 8]
        mov cx, 3
        mov dx, 0FFFFh
        I31 0008h
        CLR
        mov bx, [tmp + 8]
        mov cx, 40F2h
        I31 0009h
        push es
        mov es, [tmp + 8]
        movzx edx, byte [tag]
        shl edx, 24
        xor ebx, ebx
.cf:    lea eax, [edx + ebx]
        mov [es:ebx * 4], eax
        inc ebx
        cmp ebx, 10000h
        jb .cf
        xor ebp, ebp            ; errors
        mov dword [tmp + 12], 0 ; rounds
.cr:    mov ecx, 5
.cy:    push ecx
        mov ax, 1680h
        int 2Fh
        pop ecx
        dec ecx
        jnz .cy
        xor ebx, ebx
.cv:    lea eax, [edx + ebx]
        cmp [es:ebx * 4], eax
        je .cok
        inc ebp
.cok:   add ebx, 16
        cmp ebx, 10000h
        jb .cv
        inc dword [tmp + 12]
        cmp dword [tmp + 12], 40
        jb .cr
        pop es
        LS "C rounds="
        LH dword [tmp + 12], 2
        LS " errors="
        LH ebp, 4
        NL
        jmp L(fin)

L(fin): LS "END"
        NL
        mov ah, 3Eh
        mov bx, [logh]
        int 21h
        mov ax, 4C00h
        int 21h

; ---- helpers ----
; a selector's descriptor (BX): base, limit, access, flags; and the line's end
L(dsel):
        pushad
        push es
        push ds
        pop es
        movzx ebx, bx
        mov edi, desc
        mov ax, 000Bh
        int 31h
        pop es
        jc .bad
        mov eax, [desc + 2]
        and eax, 0FFFFFFh
        movzx ebx, byte [desc + 7]
        shl ebx, 24
        or eax, ebx
        movzx ebx, word [psp]
        shl ebx, 4
        movzx ecx, word [envseg]
        shl ecx, 4
        cmp eax, ebx
        jne .b1
        LS " base=psp"
        jmp .b3
.b1:    cmp eax, ecx
        jne .b2
        LS " base=env"
        jmp .b3
.b2:    LS " base="
        LH eax, 8
.b3:    movzx eax, word [desc]
        movzx ebx, byte [desc + 6]
        and ebx, 0Fh
        shl ebx, 16
        or eax, ebx
        test byte [desc + 6], 80h
        jz .b4
        shl eax, 12
        or eax, 0FFFh
.b4:    LS " lim="
        LH eax, 8
        movzx eax, byte [desc + 5]
        LS " acc="
        LH eax, 2
        movzx eax, byte [desc + 6]
        shr eax, 4
        LS " f="
        LH eax, 1
        jmp .d
.bad:   LS " 000B cf=1"
.d:     NL
        popad
        ret

L(pmvec):                       ; INFO: the protected-mode vector EBX
        pushad
        mov [tmp + 12], ebx
        CLR
        mov ebx, [tmp + 12]
        I31 0204h
        LS "INFO 0204 "
        LH dword [tmp + 12], 2
        LR "=", r_ecx, 4
        LR ":", r_edx, 8
        NL
        popad
        ret

L(puts):
        pushad
        mov edi, [logpos]
.l:     mov al, [esi]
        test al, al
        jz .d
        mov [logbuf + edi], al
        inc esi
        inc edi
        jmp .l
.d:     mov [logpos], edi
        popad
        ret

L(hex):
        pushad
        movzx ecx, cl
        mov edi, [logpos]
        add edi, ecx
        mov [logpos], edi
.l:     dec edi
        mov edx, eax
        and edx, 15
        mov dl, [hexd + edx]
        mov [logbuf + edi], dl
        shr eax, 4
        dec ecx
        jnz .l
        popad
        ret

; the line's end; the line to the log file (INT 21h 40h through 0300h)
L(nl):
        pushad
        push es
        mov edi, [logpos]
        mov word [logbuf + edi], 0A0Dh
        add edi, 2
        push edi
        mov edi, logr
        xor eax, eax
        mov ecx, 50 / 2
.z:     mov [edi], ax
        add edi, 2
        dec ecx
        jnz .z
        pop edi
        mov dword [logr + 1Ch], 4000h
        movzx eax, word [logh]
        mov [logr + 10h], eax
        mov [logr + 18h], edi
        mov dword [logr + 14h], logbuf
        mov ax, [psp]
        mov [logr + 24h], ax
        push ds
        pop es
        mov ax, 0300h
        mov bx, 21h
        xor ecx, ecx
        mov edi, logr
        int 31h
        mov dword [logpos], 0
        pop es
        popad
        ret

L(clrr):
        pushad
        mov edi, rmr
        xor eax, eax
        mov ecx, 50 / 2
.z:     mov [edi], ax
        add edi, 2
        dec ecx
        jnz .z
        popad
        ret

; exception EBX's handler noted (0202h) in oldex[slot]
L(exslot):
        cmp ebx, 3
        je .s1
        cmp ebx, 6
        je .s2
        cmp ebx, 0Dh
        je .s3
        mov esi, oldex
        ret
.s1:    mov esi, oldex + 8
        ret
.s2:    mov esi, oldex + 16
        ret
.s3:    mov esi, oldex + 24
        ret
L(getex):
        pushad
        call L(exslot)
        mov ax, 0202h
        int 31h
        mov [esi + 4], cx
%if BZ = 16
        movzx edx, dx
%endif
        mov [esi], edx
        popad
        ret
L(setex):                       ; EBX the exception, EDX the handler's offset
        pushad
        mov cx, cs
        mov ax, 0203h
        int 31h
        popad
        ret
L(resex):
        pushad
        call L(exslot)
        mov cx, [esi + 4]
        mov edx, [esi]
        mov ax, 0203h
        int 31h
        popad
        ret
; the exception's line: EBX the faulting instruction's offset
L(exlog):
        LS "exc "
        LH dword [exc_no], 2
        cmp dword [exc_no], 0Dh
        jne .noerr
        LS " err="
        LH dword [exc_err], 4
.noerr:
        mov eax, [exc_ip]
        cmp eax, ebx
        sete al
        movzx eax, al
        LS " ip="
        LH eax, 1
        mov eax, [exc_cs]
        mov bx, cs
        cmp ax, bx
        sete al
        movzx eax, al
        LS " cs="
        LH eax, 1
        mov eax, [exc_ss]
        mov bx, ss
        cmp ax, bx
        sete al
        movzx eax, al
        LS " ss="
        LH eax, 1
        mov eax, [exc_hss]
        mov bx, ss
        cmp ax, bx
        setne al
        movzx eax, al
        LS " hoststack="
        LH eax, 1
        LS " hss="
        movzx eax, word [exc_hss]
        LH eax, 4
        mov eax, [exc_fl]
        and eax, 0200h
        LS " if="
        LH eax, 4
        NL
        ret

; ---- handlers ----
%macro EXH 1
L(ex%1):
        push ebp
        SETBP
        push eax
        push ds
        mov ds, [cs:sel_ds]
        mov dword [exc_no], 0%1h
        GETF eax, 2
        mov [exc_err], eax
        GETF eax, 3
        mov [exc_ip], eax
        GETF eax, 4
        mov [exc_cs], eax
        GETF eax, 5
        mov [exc_fl], eax
        GETF eax, 7
        mov [exc_ss], eax
        mov word [exc_hss], ss
        mov eax, [exc_resume]
%if BZ = 16
        PUTF 3, ax
%else
        PUTF 3, eax
%endif
        pop ds
        pop eax
        pop ebp
        retf
%endmacro
        EXH 00
        EXH 03
        EXH 06
        EXH 0d
%unmacro EXH 1

L(int61):
        push ds
        mov ds, [cs:sel_ds]
        inc dword [int61cnt]
        pop ds
        iret
L(int3):
        push ds
        mov ds, [cs:sel_ds]
        inc dword [int3pm]
        pop ds
        iret
L(int8):
        push ds
        mov ds, [cs:sel_ds]
        inc dword [tick_pm]
        pop ds
        jmp far [cs:old8]
; the real-mode callback: DS:(E)SI the real-mode stack, ES:(E)DI the
; register structure; returns as a far return in real mode
L(cbproc):
        push eax
%if BZ = 16
        mov ax, [si]
        mov [es:di + 2Ah], ax
        mov ax, [si + 2]
        mov [es:di + 2Ch], ax
        add word [es:di + 2Eh], 4
        mov dword [es:di + 1Ch], 0BEEFh
        mov ax, [si + 4]
%else
        mov ax, [esi]
        mov [es:edi + 2Ah], ax
        mov ax, [esi + 2]
        mov [es:edi + 2Ch], ax
        add word [es:edi + 2Eh], 4
        mov dword [es:edi + 1Ch], 0BEEFh
        mov ax, [esi + 4]
%endif
        push ds
        mov ds, [cs:sel_ds]
        mov [tmp + 12], ax
        inc dword [cb_cnt]
        mov [cb_es], es
%if BZ = 16
        movzx edi, di
%endif
        mov [cb_edi], edi
        pop ds
        pop eax
        iret
%unmacro GETF 2
%unmacro PUTF 2
%unmacro SETBP 0
