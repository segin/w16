; poweroff.asm - POWEROFF.COM: switch the (virtual) machine off through
; the APM BIOS, so an emulator run ends by itself when a test is done
; (test tooling). Falls back to halting with interrupts off.
;
; Build: nasm -f bin -o POWEROFF.COM poweroff.asm

        bits 16
        cpu 8086
        org 100h

        mov ax, 5301h           ; connect the real-mode interface
        xor bx, bx
        int 15h
        mov ax, 530Eh           ; driver version 1.1
        xor bx, bx
        mov cx, 0101h
        int 15h
        mov ax, 5307h           ; set power state: all devices, off
        mov bx, 0001h
        mov cx, 0003h
        int 15h
        cli
.halt:  hlt
        jmp .halt
