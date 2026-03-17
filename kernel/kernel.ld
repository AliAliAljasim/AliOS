ENTRY(_start)

/*
 * KERNEL_OFFSET is the difference between virtual and physical addresses.
 *   virtual address  = physical address + KERNEL_OFFSET
 *   physical address = virtual address  - KERNEL_OFFSET
 *
 * The kernel is loaded by the bootloader at physical 0x8000.
 * It is linked to run at virtual 0xC0008000 (3 GB + 32 KB).
 */
KERNEL_OFFSET = 0xC0000000;

SECTIONS {
    /*
     * Virtual address: 0xC0001000
     * Physical (LMA):  0x00001000  (via AT clause on each section)
     *
     * AT(expr) sets the Load Memory Address — where objcopy writes the
     * section's bytes in the flat binary. The VMA (used for all symbol
     * addresses) is controlled by the . counter.
     */
    . = 0xC0008000;

    .text   : AT(ADDR(.text)   - KERNEL_OFFSET) { *(.text)    }
    .rodata : AT(ADDR(.rodata) - KERNEL_OFFSET) { *(.rodata*) }
    .data   : AT(ADDR(.data)   - KERNEL_OFFSET) { *(.data)    }
    .bss    : AT(ADDR(.bss)    - KERNEL_OFFSET) { *(.bss) *(COMMON) }

    /* Virtual address of the first byte past the kernel image.
       pmm_init() uses (kernel_end - KERNEL_OFFSET) as the physical end. */
    kernel_end = .;
}
