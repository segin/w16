; mbr.asm - a minimal master boot record (test tooling: makes the bootable
; hard disk images that run real DOS under DOSBox, see tools/mkhdd.py).
;
; Moves itself from 0:7C00 to 0:0600, finds the active partition, reads
; its first sector to 0:7C00 through INT 13h (CHS from the partition
; entry), and jumps there with DL = boot drive and DS:SI = the entry.
;
; Build: nasm -f bin -o mbr.bin mbr.asm   (446 bytes of code + padding;
; tools/mkhdd.py adds the partition table and signature)

        bits 16
        cpu 8086
        org 0600h

start:
        cli
        xor ax, ax
        mov ss, ax
        mov sp, 7C00h
        mov ds, ax
        mov es, ax
        sti
        cld
        mov si, 7C00h
        mov di, 0600h
        mov cx, 256
        rep movsw
        jmp 0:moved

moved:
        mov si, 0600h + 1BEh            ; partition table
        mov cx, 4
.find:  cmp byte [si], 80h
        je found
        add si, 16
        loop .find
        mov si, msg_noactive
        jmp fail

found:
        mov di, 5                       ; tries
.read:  mov dh, [si + 1]                ; head
        mov cx, [si + 2]                ; sector, cylinder
        mov bx, 7C00h
        mov ax, 0201h
        push si
        push di
        int 13h
        pop di
        pop si
        jnc .loaded
        xor ax, ax                      ; reset and retry
        int 13h
        dec di
        jnz .read
        mov si, msg_ioerr
        jmp fail
.loaded:
        cmp word [7DFEh], 0AA55h
        je .go
        mov si, msg_nosys
        jmp fail
.go:
%ifdef MARK
        ; debug: write "MBR OK" plus the boot sector to cylinder 0, head 0,
        ; sector 2, to show how far booting got
        push si
        push dx
        mov ax, 0301h
        mov cx, 2
        xor dh, dh
        mov bx, 7C00h
        int 13h
        pop dx
        pop si
%endif
        jmp 0:7C00h

fail:   lodsb
        or al, al
        jz .halt
        mov ah, 0Eh
        mov bx, 7
        int 10h
        jmp fail
.halt:  hlt
        jmp .halt

msg_noactive db "No active partition", 0
msg_ioerr    db "Disk read error", 0
msg_nosys    db "Missing operating system", 0

        times 446 - ($ - $$) db 0
