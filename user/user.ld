/*
 * user.ld — linker script for AliOS user-mode programs.
 *
 * Loads text at 0x08000000, which falls within the safe user address range
 * (0x08000000–0xBFFFFFFF) defined in uvm.h.  The kernel maps user stacks at
 * 0xBFFFF000, so programs must not grow beyond ~3 GB.
 *
 * All output sections are placed contiguously so the ELF has a single
 * PT_LOAD segment, keeping the loader simple.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x08000000;

    .text   : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data) }
    .bss    : { *(.bss) *(COMMON) }

    /* Discard everything the kernel doesn't need at load time. */
    /DISCARD/ : {
        *(.note.*) *(.gnu.*) *(.eh_frame*) *(.comment)
        *(.dynamic) *(.interp) *(.dynsym) *(.dynstr) *(.gnu.hash)
        *(.plt) *(.got) *(.got.plt)
    }
}
