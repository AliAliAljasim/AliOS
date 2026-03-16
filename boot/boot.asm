org 0x7C00
bits 16

KERNEL_LOAD_ADDR equ 0x1000
KERNEL_SECTORS   equ 32

start:
    mov [BOOT_DRIVE], dl

    mov si, bootmsg
    call print_string

    call load_kernel

    mov si, pmmsg
    call print_string

    call detect_memory
    call enable_a20
    cli
    lgdt [gdt_desc]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp dword CODE_SEL:pm_entry

; --------------------------
; BIOS print (real mode)
; --------------------------
print_string:
    lodsb
    cmp al, 0
    je .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

; --------------------------
; BIOS read sectors into 0000:KERNEL_LOAD_ADDR
; --------------------------
load_kernel:
    xor ax, ax
    mov es, ax
    mov bx, KERNEL_LOAD_ADDR

    mov ah, 0x02
    mov al, KERNEL_SECTORS
    mov ch, 0x00
    mov dh, 0x00
    mov cl, 0x02
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
    ret

disk_error:
    mov si, errmsg
    call print_string
    jmp $

; --------------------------
; Detect memory via INT 0x15, E820
;   count stored at 0x4FC (uint16)
;   entries stored at 0x500 (24 bytes each)
; --------------------------
detect_memory:
    pusha
    push es

    xor ax, ax
    mov es, ax              ; ES = 0
    mov di, 0x500           ; ES:DI → entry buffer
    xor ebx, ebx            ; EBX = 0 starts the list
    mov word [0x4FC], 0     ; entry count = 0

.next:
    mov eax, 0xE820
    mov ecx, 24             ; request 24-byte entries
    mov edx, 0x534D4150     ; "SMAP" magic
    int 0x15
    jc  .done               ; CF=1: error or end of list
    cmp eax, 0x534D4150     ; BIOS must echo the magic
    jne .done

    add di, 24
    inc word [0x4FC]
    cmp word [0x4FC], 32    ; safety cap — never overflow the buffer
    jge .done

    test ebx, ebx           ; EBX=0 after call means last entry
    jnz .next

.done:
    pop es
    popa
    ret

; --------------------------
; A20 enable (fast A20)
; --------------------------
enable_a20:
    in al, 0x92
    or al, 00000010b
    out 0x92, al
    ret

; --------------------------
; GDT (flat segments)
; --------------------------
gdt_start:
gdt_null: dq 0x0000000000000000
gdt_code: dq 0x00CF9A000000FFFF
gdt_data: dq 0x00CF92000000FFFF
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEL equ gdt_code - gdt_start
DATA_SEL equ gdt_data - gdt_start

; --------------------------
; 32-bit entry (jump to kernel)
; --------------------------
bits 32
pm_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x90000

    jmp CODE_SEL:KERNEL_LOAD_ADDR  ; jump into kernel at 0x1000

; --------------------------
; Data
; --------------------------
bits 16
BOOT_DRIVE db 0

bootmsg db 13,10,"[Bootloader] AliOS starting...",13,10,0
pmmsg   db "[Bootloader] Switching to protected mode...",13,10,0
errmsg  db 13,10,"[Bootloader] Disk read error!",13,10,0

times 510-($-$$) db 0
dw 0xAA55