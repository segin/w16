; pgt.asm - PGT.COM: the DOS side of the memory services' probe VxD
; VXH.386 (tests/vxd/vxh.c, tests/memsvc.sh, docs/notes/
; win386-memsvc-audit.md): run in DOS boxes under Windows 3.1's WIN386 and
; w16's, it touches the page VXH hooked (Hook_V86_Page) and memory VXH
; maps into the box, and reports what it read through VXH's V86 API
; (INT 2Fh 1684h, device 7E16h), which writes it to COM1. Black box: only
; what the memory reads back.
;
;   usage: PGT 1|2|3
;     1  the first box: VXH's look at the VM (API 0); 4F0h set to 5031h;
;        the hooked page read and written (the callback maps the VM's own
;        page), its entry (2), made not present again (3) and read, made
;        read-only (4), read and written; a page of PGT's own memory
;        mapped over by _MapIntoV86 (5), _PhysIntoV86 (10),
;        _LinMapIntoV86 (7), written through CB_High_Linear (8),
;        Map_Lin_To_VM_Addr (9), the nul page (6); an invalid opcode
;        (0Fh FFh 'W' 'H') that VXH's Hook_V86_Fault takes and skips
;     2  the second box: the same up to the read-only page (4F0h 5032h),
;        then 2000 hooked faults timed (BIOS ticks)
;     3  the first box again: its hooked page still its own; last, VXH
;        reads the hooked page from ring 0 through the high linear address
; Report codes (VXH logs "pgt CODE: CX DX"): 10h-14h the hooked page's
; bytes, 20h-24h the mapped page's, 30h the invalid opcode (1: reflected
; to INT 6, 0: taken by the hook), 50h/51h box 1's page again, 80h and up
; timings (left out of the comparison).
        bits 16
        cpu 386
        org 100h

start:  mov si, 81h
.skip:  lodsb
        cmp al, ' '
        je .skip
        sub al, '0'
        mov [mode], al
        mov ax, 1684h
        mov bx, 7E16h
        xor di, di
        mov es, di
        int 2Fh
        mov ax, es
        or ax, di
        jnz .api
        mov dx, msg_noapi
        mov ah, 9
        int 21h
        mov ax, 4C01h
        int 21h
.api:   mov [api], di
        mov [api + 2], es
        ; the page of PGT's own memory to map over: the first whole page
        ; from offset 8000h
        mov ax, cs
        movzx eax, ax
        shl eax, 4
        add eax, 8000h + 0FFFh
        shr eax, 12
        mov [bufpg], ax
        xor ax, ax                      ; 0: the VM (BX back: the hooked page)
        call far [api]
        mov [hpg], bx
        cmp byte [mode], 3
        je mode3
        ; 4F0h (the BIOS's intra-application area: instance data)
        xor ax, ax
        mov es, ax
        mov ax, 5030h
        add al, [mode]
        mov [es:4F0h], ax
        ; the hooked page
        mov ax, [hpg]
        shl ax, 8
        mov es, ax
        mov al, [es:0]
        mov bx, 10h
        call report
        mov byte [es:1], 77h
        mov al, [es:1]
        mov bx, 11h
        call report
        mov ax, 2
        call far [api]
        xor dx, dx
        mov ax, 3                       ; not present again
        call far [api]
        mov al, [es:0]
        mov bx, 12h
        call report
        mov ax, 4                       ; read-only
        call far [api]
        mov al, [es:2]
        mov bx, 13h
        call report
        mov byte [es:3], 66h
        mov al, [es:3]
        mov bx, 14h
        call report
        mov ax, 2
        call far [api]
        cmp byte [mode], 2
        je timing
        ; PGT's own page, mapped over
        mov ax, [bufpg]
        shl ax, 8
        mov es, ax
        mov byte [es:0], 11h
        mov ax, 5                       ; _MapIntoV86 of a PG_VM page
        mov cx, [bufpg]
        call far [api]
        mov al, [es:0]
        mov bx, 20h
        call report
        mov ax, 10                      ; _PhysIntoV86 of the same
        mov cx, [bufpg]
        call far [api]
        mov al, [es:0]
        mov bx, 21h
        call report
        mov ax, 7                       ; _LinMapIntoV86 of VXH's PG_SYS page
        mov cx, [bufpg]
        call far [api]
        mov al, [es:0]
        mov bx, 22h
        call report
        mov ax, 8                       ; written through CB_High_Linear
        mov cx, [bufpg]
        call far [api]
        mov al, [es:10h]
        mov bx, 23h
        call report
        mov ax, 9
        mov cx, [bufpg]
        call far [api]
        mov ax, 6                       ; the nul page
        mov cx, [bufpg]
        call far [api]
        mov al, [es:0]                  ; (no fault; what it holds is random)
        xor al, al
        mov bx, 24h
        call report
        ; the invalid opcode, with INT 6 caught in case it is reflected
        mov ax, 3506h
        int 21h
        mov [old6], bx
        mov [old6 + 2], es
        mov dx, int6
        mov ax, 2506h
        int 21h
        mov byte [saw6], 0
        db 0Fh, 0FFh, 'W', 'H'
        push ds
        lds dx, [old6]
        mov ax, 2506h
        int 21h
        pop ds
        mov al, [saw6]
        mov bx, 30h
        call report
        jmp done

timing: mov ax, 11                      ; quiet hooks
        mov dx, 1
        call far [api]
        xor ax, ax
        mov fs, ax
        mov ebx, [fs:46Ch]
        mov [t0], ebx
        mov cx, 2000
.loop:  push cx
        mov ax, 3
        mov dx, 1
        call far [api]
        mov al, [es:0]
        pop cx
        loop .loop
        mov ebx, [fs:46Ch]
        sub ebx, [t0]
        mov ax, bx
        mov bx, 80h
        call report
        mov ax, 11
        xor dx, dx
        call far [api]
        jmp done

mode3:  mov ax, [hpg]
        shl ax, 8
        mov es, ax
        mov al, [es:0]
        mov bx, 50h
        call report
        mov al, [es:1]
        mov bx, 51h
        call report
        mov ax, 12                      ; ring 0 through the high linear address
        call far [api]

done:   mov dx, msg_done
        mov ah, 9
        int 21h
        mov ax, 4C00h
        int 21h

; report BX: AL (CX), the mode (DX)
report: push es
        movzx cx, al
        movzx dx, byte [mode]
        mov ax, 1
        call far [api]
        pop es
        ret

; INT 6 reflected: the opcode skipped
int6:   push bp
        mov bp, sp
        add word [bp + 2], 4
        mov byte [cs:saw6], 1
        pop bp
        iret

msg_noapi db 'PGT: no VXH API', 13, 10, '$'
msg_done db 'PGT done', 13, 10, '$'
mode    db 0
saw6    db 0
api     dd 0
old6    dd 0
t0      dd 0
hpg     dw 0
bufpg   dw 0
