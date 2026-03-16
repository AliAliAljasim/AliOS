; kernel/entry.asm
bits 16

extern kmain
global _start

_start:
    cli

    ; Set DS = 0 (safe for now)
    xor ax, ax
    mov ds, ax
    mov es, ax

    ; Setup stack (REQUIRED for C)
    mov ss, ax
    mov sp, 0x9000

    sti

    ; Proof kernel started
    mov ah, 0x0E
    mov al, 'K'
    int 0x10

    call kmain

.hang:
    jmp .hang
