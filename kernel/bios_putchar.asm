; kernel/bios_putchar.asm
bits 16
global bios_putchar

bios_putchar:
    push bp
    mov bp, sp
    push ax

    mov ah, 0x0E
    mov al, [bp+4]   ; char argument
    int 0x10

    pop ax
    pop bp
    ret
