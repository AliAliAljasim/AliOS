bits 32

extern isr_handler

; ISR stub with no error code — push dummy 0 so the stack layout is uniform
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

; ISR stub where the CPU already pushed an error code
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; CPU exceptions 0-31
ISR_NOERR  0    ; Division by Zero
ISR_NOERR  1    ; Debug
ISR_NOERR  2    ; Non-Maskable Interrupt
ISR_NOERR  3    ; Breakpoint
ISR_NOERR  4    ; Overflow
ISR_NOERR  5    ; Bound Range Exceeded
ISR_NOERR  6    ; Invalid Opcode
ISR_NOERR  7    ; Device Not Available
ISR_ERR    8    ; Double Fault
ISR_NOERR  9    ; Coprocessor Segment Overrun
ISR_ERR   10    ; Invalid TSS
ISR_ERR   11    ; Segment Not Present
ISR_ERR   12    ; Stack-Segment Fault
ISR_ERR   13    ; General Protection Fault
ISR_ERR   14    ; Page Fault
ISR_NOERR 15    ; Reserved
ISR_NOERR 16    ; x87 Floating-Point
ISR_ERR   17    ; Alignment Check
ISR_NOERR 18    ; Machine Check
ISR_NOERR 19    ; SIMD Floating-Point
ISR_NOERR 20    ; Virtualization
ISR_ERR   21    ; Control Protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30    ; Security Exception
ISR_NOERR 31

; Syscall gate — int 0x80 (vector 128)
; DPL=3 is set in the IDT descriptor by syscall_init(), not here.
ISR_NOERR 128

; Common handler — called by every stub above.
;
; Stack layout on entry (low → high address):
;   [ESP+0 .. ESP+28]  pusha: edi,esi,ebp,esp_dummy,ebx,edx,ecx,eax
;   [ESP+32]           int_no
;   [ESP+36]           err_code  (real or dummy 0)
;   [ESP+40]           eip       (CPU)
;   [ESP+44]           cs        (CPU)
;   [ESP+48]           eflags    (CPU)
isr_common:
    pusha

    mov ax, 0x10        ; kernel data segment selector
    mov ds, ax
    mov es, ax

    push esp            ; pass pointer to the register dump as argument
    call isr_handler
    add esp, 4

    popa
    add esp, 8          ; discard int_no and err_code
    iret

; ── IRQ stubs (hardware interrupts, vectors 32-47) ──────────────
;
; Unlike CPU exceptions, hardware IRQs never push an error code.
; Each stub pushes the IRQ number (0-15) and jumps to irq_common.
;
; Stack layout on entry to irq_common (low → high address):
;   [ESP+0 .. ESP+28]  pusha: edi,esi,ebp,esp_dummy,ebx,edx,ecx,eax
;   [ESP+32]           irq_no
;   [ESP+36]           eip    (CPU)
;   [ESP+40]           cs     (CPU)
;   [ESP+44]           eflags (CPU)

extern irq_dispatch

%macro IRQ 1
global irq%1
irq%1:
    push dword %1
    jmp irq_common
%endmacro

IRQ  0   ; PIT timer
IRQ  1   ; Keyboard
IRQ  2   ; Cascade (slave PIC)
IRQ  3   ; COM2
IRQ  4   ; COM1
IRQ  5   ; LPT2
IRQ  6   ; Floppy
IRQ  7   ; LPT1 / spurious
IRQ  8   ; RTC
IRQ  9
IRQ 10
IRQ 11
IRQ 12   ; PS/2 mouse
IRQ 13   ; FPU
IRQ 14   ; Primary ATA
IRQ 15   ; Secondary ATA / spurious

irq_common:
    pusha

    mov ax, 0x10        ; kernel data segment
    mov ds, ax
    mov es, ax

    mov eax, [esp + 32] ; irq_no sits above the 8 pusha registers
    push eax
    call irq_dispatch
    add esp, 4

    popa
    add esp, 4          ; discard irq_no
    iret

section .note.GNU-stack noalloc noexec nowrite progbits
