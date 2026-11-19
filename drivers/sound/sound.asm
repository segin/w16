; sound.asm - w16's MMSOUND.DRV: the Windows 3.1 sound driver (module
; SOUND): MessageBeep's beep and the Windows 1.x/2.x SOUND API (voice
; queues of notes played on the PC speaker). drivers/sound/README.md says
; what it is, where each fact comes from and what is verified.
;
; Interface (ordinals and names from the NE export table of Windows 3.1's
; MMSOUND.DRV, docs/exports/SOUND.txt; the API's meaning from the Windows
; SDK's Sound functions; the bytes each entry takes off the stack, what it
; returns and what it plays measured on Windows 3.1's driver with SN1,
; tests/apps/sn1.c, and DOSBox-X's audio capture, tests/soundprobe.sh):
;    1 OpenSound()                          1 (voices); S_SERDVNA when open
;    2 CloseSound()                         0; the data segment when closed
;    3 SetVoiceQueueSize(nVoice, nBytes)    0; S_SEROFM below 6 bytes
;    4 SetVoiceNote(nVoice, nValue, nLength, nCdots)  0, S_SERBDNT, S_SERQFUL
;    5 SetVoiceAccent(nVoice, nTempo, nVolume, nMode, nPitch)  0, S_SERDTP,
;                                           S_SERDMD, S_SERDPT
;    6 SetVoiceEnvelope(nVoice, nShape, nRepeat)  0 (nothing)
;    7 SetSoundNoise(nSource, nDuration)    0 (nothing)
;    8 SetVoiceSound(nVoice, lFrequency, nDuration)  0, S_SERDFQ, S_SERQFUL
;    9 StartSound()                         0
;   10 StopSound()                          0
;   11 WaitSoundState(nState)               0; S_SERDST
;   12 SyncAllVoices()                      0 (nothing)
;   13 CountVoiceNotes(nVoice)              notes left while playing, else 0
;   14 GetThresholdEvent()                  DX:AX -> the threshold event word
;   15 GetThresholdStatus()                 bit 0: voice 1 reached its
;                                           threshold (cleared by the call)
;   16 SetVoiceThreshold(nVoice, nNotes)    0
;   17 DoBeep(wType)                        USER's MessageBeep(wType): the
;                                           [sounds] sound for the type
;                                           through MMSYSTEM's sndPlaySound,
;                                           else a short 749 Hz beep
;   18 WEP(bSystemExit)                     1
; One voice; the voice argument is not looked at (3.1's driver returned
; the same for voices 0-5, 255, 256, -1).
;
; MMSYSTEM: found by GetModuleHandle at the first DoBeep or OpenSound
; that finds it loaded (findmm), then used for the beep (sndPlaySound) and
; for the ticks of playing (a 5 ms periodic timeSetEvent), as 3.1's does.
;
; Playing (measured on 3.1's): StartSound turns the speaker on (port 61h
; bits 0-1) and ticks every 5 ms: on MMSYSTEM's timer event when MMSYSTEM
; was found (its timer driver then runs channel 0 of the 8254 at 5 ms),
; else with channel 0 at 5966 counts (200 Hz; mode 2) and this driver's
; IRQ 0 handler in front of the one there before, which still gets 18.2
; ticks a second (3.1's without MMSYSTEM: also 5 ms ticks, its channel 0
; not at a fixed 5966; how, UNVERIFIED). Two ticks
; later (and any quiet left over from before) the first queued item
; starts; each sounds as channel 2's divisor for its sound ticks and then
; as divisor 16 (above hearing) for its quiet ticks; a rest is divisor 16
; throughout. When the queue is empty the speaker goes off and the event
; is killed (or channel 0 goes back to 65536). A note's ticks come from its length at the tempo
; (quarter notes a minute) in half ticks (SetVoiceNote below). Dots,
; volume, envelopes and noise change nothing.
        bits 16
        cpu 286

segment _TEXT  public align=16 class=CODE use16
segment _DATA  public align=16 class=DATA use16
group DGROUP _DATA

extern GLOBALALLOC                      ; KERNEL.15
extern GLOBALFREE                       ; KERNEL.17
extern YIELD                            ; KERNEL.29
extern GETMODULEHANDLE                  ; KERNEL.47
extern GETPROCADDRESS                   ; KERNEL.50

; error codes (Windows SDK, the Sound functions)
S_SERDVNA equ -1                        ; device not available
S_SEROFM  equ -2                        ; out of memory
S_SERMACT equ -3                        ; music active
S_SERQFUL equ -4                        ; queue full
S_SERBDNT equ -5                        ; invalid note
S_SERDTP  equ -8                        ; invalid tempo
S_SERDMD  equ -10                       ; invalid mode
S_SERDPT  equ -12                       ; invalid pitch
S_SERDFQ  equ -13                       ; invalid frequency
S_SERDST  equ -16                       ; invalid state

TICKDIV equ 5966                        ; channel 0 while playing: 200 Hz
QUIET   equ 16                          ; channel 2 between notes
BURST   equ 2                           ; StartSound's first ticks: channel
                                        ; 2 sounds what it had
ENTRY   equ 6                           ; a queue entry: divisor (0 a rest),
E_DIV   equ 0                           ; sound ticks, quiet ticks after
E_SOUND equ 2
E_QUIET equ 4
BEEPDIV equ 1593                        ; DoBeep's tone: 749 Hz
SND_ASYNC equ 1                         ; sndPlaySound's flag (Multimedia
                                        ; Programmer's Guide)
TIME_PERIODIC equ 1                     ; timeSetEvent's
MMTICK  equ 5                           ; the timer event's period and
                                        ; resolution, ms

segment _DATA
opened  db 0
playing db 0                            ; the timer is running the queue
hooked  db 0                            ; INT 8 is ours
passthru db 0                           ; hooked, nothing to play
prev8   dd 0                            ; INT 8 before StartSound
acc     dw 0                            ; channel 0 counts since the last
                                        ; tick passed on to prev8
qhandle dw 0                            ; the queue's memory (fixed)
qsel    dw 0
qcap    dw 0                            ; entries it holds
qcount  dw 0                            ; entries in it
qhead   dw 0                            ; offset of the next to play
qtail   dw 0                            ; offset of the next free one
qend    dw 0                            ; qcap * ENTRY
burst   dw 0                            ; StartSound's first ticks left
dleft   dw 0                            ; then the quiet left over
leftover dw 0                           ; quiet ticks the last item had
                                        ; left when playing ended
phase   db 0                            ; 0 none, 1 sound, 2 quiet
cursnd  dw 0                            ; the item's sound ticks left
curq    dw 0                            ; its quiet ticks left
tempo   dw 120                          ; the accent
mode    dw 0
pitch   dw 0
thresh  dw 0                            ; voice 1's threshold
tstatus dw 0                            ; GetThresholdStatus's bits
mmfound db 0                            ; MMSYSTEM found (once found, kept)
usemm   db 0                            ; playing on MMSYSTEM's timer event
timerid dw 0                            ; the event (0: none)
mmsnd   dd 0                            ; MMSYSTEM.2 sndPlaySound
mmset   dd 0                            ; MMSYSTEM.602 timeSetEvent
mmkill  dd 0                            ; MMSYSTEM.603 timeKillEvent
        times 84h - ($ - $$) db 0
tevent  dw 0                            ; +84h GetThresholdEvent's word
                                        ; (3.1's: offset 84h of its data)
mmname  db "MMSYSTEM", 0
; the beep's sounds, WIN.INI [sounds] keys, by bits 4-7 of the type
s_def   db "SystemDefault", 0
s_hand  db "SystemHand", 0
s_quest db "SystemQuestion", 0
s_excl  db "SystemExclamation", 0
s_ast   db "SystemAsterisk", 0
beepsnd dw s_def, s_hand, s_quest, s_excl, s_ast, s_def, s_def, s_def
        dw s_def, s_def, s_def, s_def, s_def, s_def, s_def, s_def
%include "sndtab.inc"

segment _TEXT

global libentry
libentry:
        mov ax, 1
        retf

; ---- 18: WEP(bSystemExit) -> 1 (SN1: 2 bytes off, DX kept)
global WEP
WEP:
        mov ax, 1
        retf 2

; ---- the stubs: 6 SetVoiceEnvelope(nVoice, nShape, nRepeat), 7
; SetSoundNoise(nSource, nDuration), 12 SyncAllVoices() -> 0, nothing
; done (3.1's: 0 for every input, DX kept; SyncAllVoices changed nothing
; that was played)
global SETVOICEENVELOPE, SETSOUNDNOISE, SYNCALLVOICES
SETVOICEENVELOPE:
        xor ax, ax
        retf 6
SETSOUNDNOISE:
        xor ax, ax
        retf 4
SYNCALLVOICES:
        xor ax, ax
        retf

; enter: DS = DGROUP, BP frame (callers keep SI, DI, DS and BP: 3.1's does)
%macro prolog 0
        push bp
        mov bp, sp
        push ds
        push si
        push di
        mov ax, DGROUP
        mov ds, ax
%endmacro
%macro epilog 1
        pop di
        pop si
        pop ds
        pop bp
        retf %1
%endmacro

; ---- 1: OpenSound() -> 1, or S_SERDVNA when already open. A queue of
; 192 bytes (32 entries; the SDK's default size), the accent tempo 120,
; normal, no transposition.
global OPENSOUND
OPENSOUND:
        prolog
        mov ax, S_SERDVNA
        cmp byte [opened], 0
        jne .r
        call findmm                     ; (3.1's: before the queue's
        mov ax, 192                     ; GlobalAlloc)
        call newqueue
        or ax, ax
        jnz .r
        mov byte [opened], 1
        mov word [tempo], 120
        mov word [mode], 0
        mov word [pitch], 0
        mov word [thresh], 0
        mov word [tstatus], 0
        mov ax, 1
        xor dx, dx                      ; (3.1's: DX 0)
.r:     epilog 0

; ---- 2: CloseSound(): playing stops, the queue goes. 0; when not open
; a value 3.1's leaves in AX (09D7h there; not its data selector): this
; one returns its data selector
global CLOSESOUND
CLOSESOUND:
        prolog
        mov ax, ds
        cmp byte [opened], 0
        je .r
        call stopall
        call freequeue
        mov byte [opened], 0
        xor ax, ax
.r:     epilog 0

; newqueue: AX = bytes (unsigned). AX = 0 (done) or S_SEROFM; the old
; queue is freed. Below 6 bytes: S_SEROFM and the old one kept, except 0:
; accepted (3.1's: 0, and the next note never returned) as an empty one.
newqueue:
        or ax, ax
        jz .zero
        cmp ax, ENTRY
        jb .oom
        push ax
        call freequeue
        pop ax
        xor dx, dx
        mov cx, ENTRY
        div cx
        mov [qcap], ax
        mul cx
        mov [qend], ax
        push ax
        push word 0                     ; GMEM_FIXED (interrupt-time data;
        push word 0                     ; 3.1's: flags 0, as SN1 part Y's
        push ax                         ; records show)
        call far GLOBALALLOC
        pop cx
        or ax, ax
        jz .nomem
        mov [qhandle], ax               ; a fixed block's handle is its
        mov [qsel], ax                  ; selector
        jmp .empty
.nomem: mov word [qcap], 0
        mov word [qend], 0
.oom:   mov ax, S_SEROFM
        ret
.zero:  call freequeue
.empty: xor ax, ax
        mov [qcount], ax
        mov [qhead], ax
        mov [qtail], ax
        ret

freequeue:
        mov ax, [qhandle]
        or ax, ax
        jz .r
        push ax
        call far GLOBALFREE
        mov word [qhandle], 0
.r:     xor ax, ax
        mov [qsel], ax
        mov [qcap], ax
        mov [qend], ax
        mov [qcount], ax
        mov [qhead], ax
        mov [qtail], ax
        ret

; ---- 3: SetVoiceQueueSize(nVoice, nBytes) -> 0 or S_SEROFM; the queued
; notes go. While playing: S_SERMACT (UNVERIFIED on 3.1's)
global SETVOICEQUEUESIZE
SETVOICEQUEUESIZE:
        prolog
        xor ax, ax
        cmp byte [opened], 0
        je .r
        mov ax, S_SERMACT
        cmp byte [playing], 0
        jne .r
        mov ax, [bp + 6]
        call newqueue
.r:     epilog 4

; additem: AX divisor (0 a rest), BX sound ticks, CX quiet ticks after
; them -> AX 0 or S_SERQFUL
additem:
        push es
        push di
        mov dx, [qcount]
        cmp dx, [qcap]
        jae .full
        mov es, [qsel]
        mov di, [qtail]
        pushf
        cli
        mov [es:di + E_DIV], ax
        mov [es:di + E_SOUND], bx
        mov [es:di + E_QUIET], cx
        add di, ENTRY
        cmp di, [qend]
        jb .w
        xor di, di
.w:     mov [qtail], di
        inc word [qcount]
        popf
        xor ax, ax
        jmp .r
.full:  mov ax, S_SERQFUL
.r:     pop di
        pop es
        ret

; ---- 4: SetVoiceNote(nVoice, nValue, nLength, nCdots): a note (1-84,
; 37 is C5) or a rest (0) of 1/nLength of a whole note, transposed by the
; accent's pitch (wrapping past 84). nLength is a byte: 0 queues nothing
; and returns nValue (3.1's did). nCdots is not looked at (3.1's played
; dotted notes as plain ones).
; The note's time, as measured on 3.1's (SN1 parts T, U and V: 78
; combinations of tempo, length and mode, every one to the tick): in half
; ticks (2.5 ms) H = 96000 / (tempo * length), truncated (4 quarter
; notes of 60000 / tempo ms); the sounding part s = 7H/8 (normal) or
; 3H/4 (staccato), truncated, or H (legato); it sounds (s + 1) / 2 ticks
; and is quiet (H - s + 1) / 2. A rest is quiet (H + 1) / 2 ticks (and
; one more when something follows it: in the tick routine).
global SETVOICENOTE
SETVOICENOTE:
        prolog
        xor ax, ax
        cmp byte [opened], 0
        je .r
        mov si, [bp + 10]               ; nValue
        mov ax, S_SERBDNT
        cmp si, 84
        ja .r                           ; (unsigned: negative too)
        mov ax, si
        mov cl, [bp + 8]                ; nLength's low byte
        or cl, cl
        jz .r
        xor ch, ch
        mov ax, [tempo]
        mul cx
        mov cx, ax
        mov dx, 0001h                   ; 96000
        mov ax, 7700h
        div cx
        mov di, ax                      ; DI = H, half ticks
        or si, si
        jz .rest
        mov bx, di                      ; legato: s = H
        mov ax, [mode]
        cmp ax, 1
        je .snd
        mov ax, di
        cmp word [mode], 2
        je .stac
        mov dx, 7                       ; normal: 7H/8
        mul dx
        shr dx, 1
        rcr ax, 1
        shr dx, 1
        rcr ax, 1
        shr dx, 1
        rcr ax, 1
        mov bx, ax
        jmp .snd
.stac:  mov dx, 3                       ; staccato: 3H/4
        mul dx
        shr dx, 1
        rcr ax, 1
        shr dx, 1
        rcr ax, 1
        mov bx, ax
.snd:   mov cx, di
        sub cx, bx
        inc cx
        shr cx, 1                       ; quiet (H - s + 1) / 2
        inc bx
        shr bx, 1                       ; sound (s + 1) / 2
        add si, [pitch]
        cmp si, 84
        jbe .n
        sub si, 84
.n:     dec si
        shl si, 1
        mov ax, [notediv + si]
        jmp .add
.rest:  mov cx, di
        inc cx
        shr cx, 1                       ; quiet (H + 1) / 2
        xor bx, bx
        xor ax, ax
.add:   call additem
.r:     epilog 8

; ---- 5: SetVoiceAccent(nVoice, nTempo, nVolume, nMode, nPitch) -> 0, or
; S_SERDTP (tempo not 32-255), S_SERDMD (mode not 0-2), S_SERDPT (pitch
; not 0-83), checked in that order (UNVERIFIED: 3.1's order); the volume
; is not looked at. For the notes queued after it.
global SETVOICEACCENT
SETVOICEACCENT:
        prolog
        xor ax, ax
        cmp byte [opened], 0
        je .r
        mov ax, S_SERDTP
        mov bx, [bp + 12]
        cmp bx, 32
        jb .r
        cmp bx, 255
        ja .r
        mov ax, S_SERDMD
        mov cx, [bp + 8]
        cmp cx, 2
        ja .r
        mov ax, S_SERDPT
        mov dx, [bp + 6]
        cmp dx, 83
        ja .r
        mov [tempo], bx
        mov [mode], cx
        mov [pitch], dx
        xor ax, ax
.r:     epilog 10

; ---- 8: SetVoiceSound(nVoice, lFrequency, nDuration): a tone of
; HIWORD(lFrequency) Hz (37-32767; 0 a rest; the fraction is not looked
; at) for nDuration / 2 ticks, rounded up, all of it sounding
global SETVOICESOUND
SETVOICESOUND:
        prolog
        xor ax, ax
        cmp byte [opened], 0
        je .r
        mov bx, [bp + 10]               ; the frequency's high word
        xor ax, ax
        or bx, bx
        jz .rest
        mov ax, S_SERDFQ
        cmp bx, 37
        jl .r
        mov dx, 12h                     ; 1193182 / f
        mov ax, 34DEh
        div bx
.rest:  mov bx, [bp + 6]
        shr bx, 1
        adc bx, 0                       ; sound (nDuration + 1) / 2, unsigned
        xor cx, cx                      ; no quiet after it
        or ax, ax
        jnz .add
        xor bx, bx                      ; frequency 0: a rest of no ticks
                                        ; (3.1's: one tick whatever the
                                        ; duration, SN1 part T)
.add:   call additem
.r:     epilog 8

; ---- 9: StartSound(): the queued items start playing (nothing when
; there are none or they already are)
global STARTSOUND
STARTSOUND:
        prolog
        cmp byte [opened], 0
        je .r
        cmp byte [playing], 0
        jne .r
        cmp word [qcount], 0
        je .r
        call unhook                     ; a pass-through hook left over
        cli
        mov word [burst], BURST
        mov ax, [leftover]
        mov [dleft], ax
        mov word [leftover], 0
        mov byte [phase], 0
        mov byte [playing], 1
        ; MMSYSTEM found: the ticks come from its timer event, made with
        ; interrupts off (3.1's: timeSetEvent(5, 5, its callback, 0,
        ; TIME_PERIODIC), SN1 part Y; MMSYSTEM's timer driver runs the
        ; 8254 at its 5 ms resolution then). The speaker goes on after
        ; it (3.1's first start after the boot, when the timer driver
        ; first sets its resolution, sounded the old tone for 10 ms, as
        ; every later start; turned on before the call, 18 ms)
        cmp word [mmset + 2], 0
        je .irq0
        push word MMTICK
        push word MMTICK
        push cs
        push word mmtick
        push word 0
        push word 0
        push word TIME_PERIODIC
        call far [mmset]
        or ax, ax
        jz .irq0                        ; no event (no timer driver):
        mov [timerid], ax               ; INT 8 (UNVERIFIED on 3.1's)
        mov byte [usemm], 1
        cli
        call spkon
        sti
        jmp .r
.irq0:  sti
        call hook
        cli
        mov byte [passthru], 0
        call spkon
        mov al, 34h                     ; channel 0: 200 Hz, mode 2
        out 43h, al
        mov ax, TICKDIV
        out 40h, al
        mov al, ah
        out 40h, al
        sti
.r:     xor ax, ax
        epilog 0

; spkon: the speaker on (channel 2 keeps what it had: 3.1's sounded it
; for the first two ticks)
spkon:
        in al, 61h
        or al, 3
        out 61h, al
        ret

; ---- 10: StopSound(): playing stops, the queue is emptied
global STOPSOUND
STOPSOUND:
        prolog
        cmp byte [opened], 0
        je .r
        call stopall
.r:     xor ax, ax
        epilog 0

; stopall: nothing plays, nothing is queued, INT 8 given back
stopall:
        cli
        ; stopped while an item plays: its quiet is left over for the
        ; next StartSound, as when playing ends (3.1's: closed during a
        ; normal quarter note, the next start came 2 + 13 ticks later)
        cmp byte [playing], 0
        je .fin
        mov ax, [curq]
        cmp byte [phase], 0
        jne .lo
        mov ax, [dleft]
.lo:    mov [leftover], ax
        call finish
        jmp .q
.fin:   call mmstop                     ; not playing: 3.1's still kills
                                        ; its event (timeKillEvent(0) when
                                        ; none, interrupts off; SN1 part Y)
.q:     mov ax, QUIET                   ; channel 2 quiet (3.1's: no burst
        call ch2                        ; of the old tone at the next start)
        xor ax, ax
        mov [qcount], ax
        mov [qhead], ax
        mov [qtail], ax
        mov [tstatus], ax               ; (3.1's: no status after it)
        sti
        call unhook
        ret

; finish (interrupts off): the speaker off; on MMSYSTEM's timer the event
; is killed (3.1's: from its callback at the last tick, or StopSound or
; CloseSound), else channel 0 goes back to 65536 (mode 3, as the BIOS has
; it) and the hook passes ticks on
finish:
        cmp byte [playing], 0
        je .r
        mov byte [playing], 0
        mov byte [phase], 0
        in al, 61h
        and al, 0FCh
        out 61h, al
        cmp byte [usemm], 0
        jne mmstop
        mov byte [passthru], 1
        mov al, 36h
        out 43h, al
        xor al, al
        out 40h, al
        out 40h, al
.r:     ret

; mmstop (interrupts off): timeKillEvent(the event, 0 when none), when
; MMSYSTEM was found; no event after it
mmstop:
        cmp word [mmkill + 2], 0
        je .r
        push bx
        push cx
        push dx
        push es
        push word [timerid]
        call far [mmkill]
        pop es
        pop dx
        pop cx
        pop bx
        cli                             ; (whatever MMSYSTEM did)
        mov word [timerid], 0
        mov byte [usemm], 0
.r:     ret

; hook: INT 8 (protected mode) to tick_int, the old one kept
hook:
        cmp byte [hooked], 0
        jne .r
        push es
        push bx
        mov ax, 3508h
        int 21h
        mov [prev8], bx
        mov [prev8 + 2], es
        pop bx
        pop es
        mov word [acc], 0
        mov byte [passthru], 1
        mov byte [hooked], 1
        push ds
        push cs
        pop ds
        mov dx, tick_int
        mov ax, 2508h
        int 21h
        pop ds
.r:     ret

; unhook (task time): INT 8 back, unless still playing
unhook:
        cmp byte [hooked], 0
        je .r
        cmp byte [playing], 0
        jne .r
        mov byte [hooked], 0
        push ds
        lds dx, [prev8]
        mov ax, 2508h
        int 21h
        pop ds
.r:     ret

; ---- 11: WaitSoundState(nState) -> 0 when the queue has played
; (S_QUEUEEMPTY 0) or voice 1 has reached its threshold (S_THRESHOLD 1,
; S_ALLTHRESHOLD 2: one voice), yielding meanwhile; S_SERDST for any other
; state. (3.1's never came back from S_THRESHOLD and S_ALLTHRESHOLD with
; nothing playing; this one does.)
global WAITSOUNDSTATE
WAITSOUNDSTATE:
        prolog
        mov bx, [bp + 6]
        mov ax, S_SERDST
        cmp bx, 2
        ja .r
        cmp byte [opened], 0
        je .done
.wait:  cmp byte [playing], 0
        je .done
        or bx, bx
        jz .y
        mov ax, [qcount]                ; S_THRESHOLD: the count down to
        cmp ax, [thresh]                ; the threshold (3.1's came back
        jbe .done                       ; at count 2, threshold 2)
.y:     push bx
        call far YIELD
        pop bx
        jmp .wait
.done:  call unhook
        xor ax, ax
.r:     epilog 2

; ---- 13: CountVoiceNotes(nVoice): while playing, the items not yet
; started, and 1 while the last one plays; 0 when not playing (3.1's:
; notes queued and not started count 0)
global COUNTVOICENOTES
COUNTVOICENOTES:
        prolog
        xor ax, ax
        cmp byte [playing], 0
        je .r
        mov ax, [qcount]
        or ax, ax
        jnz .r
        inc ax
.r:     epilog 2

; ---- 14: GetThresholdEvent() -> DX:AX, the threshold event word (3.1's:
; in its data segment, 0)
global GETTHRESHOLDEVENT
GETTHRESHOLDEVENT:
        prolog
        xor ax, ax                      ; closed: 0 (3.1's)
        mov dx, ds
        cmp byte [opened], 0
        je .r
        mov ax, tevent
.r:     epilog 0

; ---- 15: GetThresholdStatus() -> the voices (bit 0: voice 1) whose
; count has come down to their threshold since the last call; cleared
global GETTHRESHOLDSTATUS
GETTHRESHOLDSTATUS:
        prolog
        xor ax, ax
        cmp byte [opened], 0
        je .r
        cli
        xchg ax, [tstatus]
        sti
.r:     epilog 0

; ---- 16: SetVoiceThreshold(nVoice, nNotes) -> 0. The status bit is set
; at once when more than nNotes items are queued (3.1's: 4 queued, not
; started, threshold 2: set), and when playing brings the count down to
; nNotes.
global SETVOICETHRESHOLD
SETVOICETHRESHOLD:
        prolog
        xor ax, ax
        cmp byte [opened], 0
        je .r
        mov bx, [bp + 6]
        mov [thresh], bx
        cmp [qcount], bx
        jbe .r
        or word [tstatus], 1
.r:     epilog 4

; findmm: MMSYSTEM, until it is found (3.1's, SN1 parts M, L, Y: at every
; DoBeep but FFFFh's and every OpenSound until GetModuleHandle("MMSYSTEM")
; finds it loaded, never LoadLibrary; then sndPlaySound, timeSetEvent and
; timeKillEvent by ordinal (2, 602, 603), and timeKillEvent of the event
; there is (none: 0); kept from then on). DS = DGROUP; AX, BX, CX, DX,
; ES changed.
findmm:
        cmp byte [mmfound], 0
        jne .r
        push ds
        push word mmname
        call far GETMODULEHANDLE
        or ax, ax
        jz .r
        mov bx, ax
        mov cx, 2
        call .proc
        mov [mmsnd], ax
        mov [mmsnd + 2], dx
        mov cx, 602
        call .proc
        mov [mmset], ax
        mov [mmset + 2], dx
        mov cx, 603
        call .proc
        mov [mmkill], ax
        mov [mmkill + 2], dx
        mov byte [mmfound], 1
        cmp word [mmkill + 2], 0
        je .r
        push word [timerid]
        call far [mmkill]
.r:     ret
.proc:  push bx                         ; GetProcAddress(BX, ordinal CX)
        push bx
        push word 0
        push cx
        call far GETPROCADDRESS
        pop bx
        ret

; mmtick: the timer event's callback (MMSYSTEM's timeSetEvent; a FAR
; PASCAL (id, msg, dwUser, dw1, dw2), called with interrupts on): one
; tick, with interrupts off
mmtick:
        push bp
        mov bp, sp
        push ds
        push ax
        push bx
        push cx
        push dx
        push es
        mov ax, DGROUP
        mov ds, ax
        pushf
        cli
        cmp byte [usemm], 0
        je .n
        call tick
.n:     popf
        pop es
        pop dx
        pop cx
        pop bx
        pop ax
        pop ds
        pop bp
        retf 16

; ---- 17: DoBeep(wType), USER's MessageBeep(wType) (3.1's, SN1 parts M,
; N, I, R, Z): FFFFh beeps on the speaker; any other type, once MMSYSTEM
; is found (findmm), is handed to sndPlaySound(name, SND_ASYNC) at once
; (interrupts on or off), by bits 4-7 of the type: 1 SystemHand, 2
; SystemQuestion, 3 SystemExclamation, 4 SystemAsterisk, any other
; SystemDefault (MMSYSTEM looks the name up in WIN.INI [sounds]); the
; speaker beeps when MMSYSTEM is not loaded or sndPlaySound answers FALSE
; (no wave device, no sound for the name and none for SystemDefault).
; AX = sndPlaySound's answer when it played (3.1's: 1).
global DOBEEP
DOBEEP:
        push bp
        mov bp, sp
        push ds
        mov ax, DGROUP
        mov ds, ax
        mov bx, [bp + 6]
        cmp bx, 0FFFFh
        je .spk
        push bx
        call findmm
        pop bx
        cmp word [mmsnd + 2], 0
        je .spk
        shr bx, 3
        and bx, 1Eh
        push ds
        push word [beepsnd + bx]
        push word SND_ASYNC
        call far [mmsnd]
        or ax, ax
        jz .spk
        pop ds
        pop bp
        retf 2
.spk:   pop ds
        pop bp
        ; the speaker: channel 2 at 1593 (749 Hz), on for about 64000
        ; instructions, then off (3.1's: 21.4 ms at 3000 DOSBox cycles a
        ; ms, 6.7 at 10000: a loop, not a timer). AX and DX are left as
        ; the loop leaves them (3.1's: DX 500).
        pushf
        cli
        mov al, 0B6h
        out 43h, al
        mov ax, BEEPDIV
        out 42h, al
        mov al, ah
        out 42h, al
        in al, 61h
        or al, 3
        out 61h, al
        popf
        mov dx, 500
.o:     mov cx, 126
.i:     loop .i
        dec dx
        jnz .o
        pushf
        cli
        in al, 61h
        and al, 0FCh
        out 61h, al
        popf
        mov dx, 500
        xor ax, ax
        retf 2

; ---- INT 8 (IRQ 0), protected mode, while hooked: 200 ticks a second
; while playing. The handler before gets one tick for every 65536
; channel 0 counts (the BIOS's 18.2 a second). Touches only this
; driver's fixed data and the queue's fixed block. The queue's tick is
; done first, with interrupts off; then the old handler is called (it
; ends the interrupt: the BIOS's, DOSX's, SYSTEM.DRV's, which enables
; interrupts for its timer procedures) or the interrupt is ended here.
tick_int:
        sub sp, 4                       ; [bp+6] the old handler's address
        push ds                         ; when it is jumped to
        push ax
        push bp
        mov bp, sp
        mov ax, DGROUP
        mov ds, ax
        cmp byte [passthru], 0
        je .play
        mov ax, [prev8]                 ; on to the old handler, nothing
        mov [bp + 6], ax                ; of the stack changed
        mov ax, [prev8 + 2]
        mov [bp + 8], ax
        pop bp
        pop ax
        pop ds
        retf
.play:  push bx
        push cx
        push es
        call tick
        pop es
        pop cx
        pop bx
        add word [acc], TICKDIV
        jnc .eoi
        pushf
        call far [prev8]
        jmp .out
.eoi:   mov al, 20h
        out 20h, al
.out:   pop bp
        pop ax
        pop ds
        add sp, 4                       ; (the flags come back with IRET)
        iret

; tick: one 5 ms tick of the queue (DS = DGROUP, interrupts off). As
; measured on 3.1's (SN1 parts D, H, P, S, T; the recordings):
;  - StartSound's first 2 ticks channel 2 sounds what it had; then the
;    quiet the last item played had left when playing ended before
;    (divisor 16), then the first item;
;  - a note sounds its sound ticks, then is quiet for its quiet ticks; a
;    rest is quiet for its ticks and one more;
;  - when the queue is empty as an item's sound ends (or as a rest
;    starts), the speaker goes off (channel 2 keeps the note) and playing
;    ends: the item's quiet is left over for the next StartSound (also
;    when StopSound or CloseSound stop it, which make channel 2 quiet);
;  - the count goes down as an item starts; the threshold status bit is
;    set when it comes down to the threshold.
tick:
        cmp byte [playing], 0
        je .r
        cmp word [burst], 0
        je .left
        dec word [burst]
        jnz .r
        cmp word [dleft], 0
        je fetch
        mov ax, QUIET
        jmp ch2
.left:  cmp word [dleft], 0
        je .phase
        dec word [dleft]
        jnz .r
        jmp fetch
.phase: cmp byte [phase], 1
        jne .quiet
        dec word [cursnd]
        jz soundover
        ret
.quiet: cmp byte [phase], 2
        jne fetch
        dec word [curq]
        jnz .r
        jmp fetch
.r:     ret

; soundover: the item's sound has ended
soundover:
        cmp word [curq], 0              ; quiet after it: channel 2 quiet
        je .q                           ; (3.1's: a note with no quiet,
        mov ax, QUIET                   ; legato or SetVoiceSound, left
        call ch2                        ; its tone in channel 2 at the end;
.q:     cmp word [qcount], 0            ; a normal one left it quiet: the
        jne .more                       ; next start had no burst)
        mov ax, [curq]
        mov [leftover], ax
        jmp startend
.more:  mov byte [phase], 2
        cmp word [curq], 0
        je fetch
        ret

; fetch: the next item starts
fetch:
        cmp word [qcount], 0
        jne .take
        mov word [leftover], 0
        jmp startend
.take:  push es
        mov es, [qsel]
        mov bx, [qhead]
        mov ax, [es:bx + E_DIV]
        mov cx, [es:bx + E_SOUND]
        mov [cursnd], cx
        mov cx, [es:bx + E_QUIET]
        mov [curq], cx
        pop es
        add bx, ENTRY
        cmp bx, [qend]
        jb .h
        xor bx, bx
.h:     mov [qhead], bx
        dec word [qcount]
        mov bx, [qcount]
        cmp bx, [thresh]
        jne .t
        or word [tstatus], 1
.t:     or ax, ax
        jnz .note
        mov ax, QUIET                   ; a rest
        call ch2
        cmp word [qcount], 0
        jne .rest
        mov ax, [curq]
        mov [leftover], ax
        jmp startend
.rest:  inc word [curq]
        mov byte [phase], 2
        ret
.note:
        call ch2
        mov byte [phase], 1
        cmp word [cursnd], 0
        je soundover
        ret

; startend: the speaker off; playing ends at the next tick
startend:
        jmp finish

; ch2: channel 2's count = AX, without a control word (the mode stays as
; it was: 3, the BIOS's and DoBeep's). On 3.1's, recorded, every note's
; first half-period is about 125 us long and the rest even, as a count
; written alone gives in DOSBox-X; with a control word and the count the
; first is even. (UNVERIFIED on a real 8254, which takes a count written
; alone in mode 3 at the end of the half-cycle under way.)
ch2:
        out 42h, al
        mov al, ah
        out 42h, al
        ret
