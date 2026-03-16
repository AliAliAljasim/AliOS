bits 32
global _start
global kernel_stack_top
extern kmain

; ── Constants ────────────────────────────────────────────────────────────────
;
; The kernel is *linked* at 0xC0001000 (virtual) but *loaded* at physical
; 0x1000. Every symbol address in this file is a virtual address.
; Before paging is on, subtract KERNEL_OFFSET to reach the physical address.

KERNEL_OFFSET equ 0xC0000000

; ── BSS objects ──────────────────────────────────────────────────────────────
;
; Both objects live in BSS so they cost nothing in the binary.
; QEMU zeroes all RAM before execution, so BSS starts out zeroed. ✓

section .bss

; Temporary page directory used during boot (before paging_init() runs).
; Must be 4 KB-aligned so the CPU accepts it as a CR3 value.
align 4096
boot_page_dir: resb 4096

; Kernel stack: 16 KB, grows downward from kernel_stack_top.
align 16
kernel_stack_bottom: resb 16384
kernel_stack_top:

; ── Entry point ──────────────────────────────────────────────────────────────

section .text

_start:
    ; ── Phase 1: we are at physical 0x1000, paging is OFF ────────────────
    ;
    ; Virtual addresses are not usable yet. Any symbol address must have
    ; KERNEL_OFFSET subtracted to obtain the physical address.

    ; Set up the two PDEs we need in boot_page_dir.
    ;   PDE[0]   = 0x00000083  identity-map first 4 MB  (P=1, RW=1, PS=1)
    ;   PDE[768] = 0x00000083  map 0xC0000000 → 0x00000000  (same flags)
    ;
    ; 768 = 0xC0000000 >> 22  (the kernel's page-directory index)
    ; PS=1 uses 4 MB pages, so we need no separate page tables yet.
    ; 0x83 = 0b10000011 = PS(bit7) | RW(bit1) | P(bit0)

    mov eax, (boot_page_dir - KERNEL_OFFSET)

    mov dword [eax + 0   * 4], 0x00000083   ; PDE[0]:   identity first 4 MB
    mov dword [eax + 768 * 4], 0x00000083   ; PDE[768]: kernel high first 4 MB

    ; Enable CR4.PSE so the CPU honours the PS bit in PDEs (4 MB pages).
    mov eax, cr4
    or  eax, 0x10
    mov cr4, eax

    ; Load CR3 with the *physical* address of boot_page_dir.
    mov eax, (boot_page_dir - KERNEL_OFFSET)
    mov cr3, eax

    ; Set CR0.PG to enable paging.
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    ; ── Phase 2: paging is ON, but EIP is still physical (~0x1xxx) ───────
    ;
    ; Jump to the kernel's virtual address space. A plain relative jmp would
    ; use the relative offset computed from VMA addresses; adding that offset
    ; to the current physical EIP would land at the wrong address. Load the
    ; full VMA into a register and jump through it instead.

    mov eax, higher_half    ; EAX = virtual address of higher_half
    jmp eax                 ; EIP ← VMA; CPU looks it up via PDE[768] ✓

higher_half:
    ; ── Phase 3: now executing at virtual 0xC0001xxx ─────────────────────
    ;
    ; The low identity map (PDE[0]) is kept active until paging_init()
    ; installs the full page directory. This allows vga_init(), pmm_init()
    ; and other early subsystems to reach low physical memory (VGA at 0xB8000,
    ; E820 table at 0x500, etc.) before the wider identity map is live.

    ; Point the stack at its virtual (high) address.
    mov esp, kernel_stack_top

    ; Call the C kernel. kmain() should never return.
    call kmain

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
