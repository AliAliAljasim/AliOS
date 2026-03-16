org 0x1000

start:
    mov ah, 0x0E
    mov si, msg

.print:
    lodsb
    cmp al, 0
    je .hang
    int 0x10
    jmp .print

.hang:
    jmp .hang

msg db 13,10,"[Kernel] Hello from the kernel!",13,10,0
