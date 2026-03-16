#include "serial.h"
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "fault.h"
#include "syscall.h"
#include "irq.h"
#include "timer.h"
#include "keyboard.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "ata.h"
#include "alfs.h"
#include "elf.h"
#include "shell.h"
#include "task.h"
#include "sched.h"
#include "uvm.h"
#include <stdint.h>


void kmain(void)
{
    serial_init();
    vga_init();
    gdt_init();
    idt_init();
    fault_init();
    syscall_init();
    irq_init();
    timer_init(100);
    keyboard_init();
    pmm_init();
    paging_init();
    heap_init();
    ata_init();
    int fs_ok = (alfs_init() == 0);
    task_init();

    __asm__ volatile ("sti");

    vga_puts("AliOS\n\n");

    /* Show detected free memory */
    uint32_t pages = pmm_free_pages();
    vga_puts("PMM: ");
    vga_printdec(pages);
    vga_puts(" free pages (");
    vga_printdec(pages / 256);       /* 256 pages = 1 MB */
    vga_puts(" MB)\n");
    vga_puts("GDT:     OK\n");
    vga_puts("Syscall: OK\n");
    vga_puts("Paging:  OK\n");
    vga_puts("Heap:    OK\n");
    vga_puts("ATA:     OK\n");
    vga_puts(fs_ok ? "FS:      OK\n" : "FS:      (no fs)\n");
    vga_puts("Tasks:   OK\n\n");

    /*
     * Activate the scheduler FIRST (closes the run queue as a circular list
     * of one: the boot task).  sched_add() calls after sched_init() insert
     * tasks into the already-circular list so they are not orphaned.
     */
    sched_init();

    /* ── Load and launch ELF user task ──────────────────────────────────
     *
     * 1. Create a fresh page directory (kernel mappings pre-populated).
     * 2. Parse the ELF binary from AliFS and map each PT_LOAD segment.
     * 3. Allocate one page for the user stack at 0xBFFFF000.
     * 4. Create a ring-3 task at the ELF entry point and schedule it.
     */
    uint32_t *upd   = uvm_create();
    uint32_t  entry = 0;
    if (upd && elf_load(upd, "hello", &entry) == 0) {
        uint32_t sp = pmm_alloc();
        if (sp) {
            uvm_map(upd, 0xBFFFF000, sp, PAGE_WRITE | PAGE_USER);
            task_t *ut = task_create_user(entry, 0xBFFFFFFC, upd);
            if (ut) {
                sched_add(ut);
                vga_puts("ELF:     OK\n\n");
            }
        }
    } else {
        vga_puts("ELF:     (load failed)\n\n");
    }

    /* The boot task becomes the interactive shell — never returns. */
    shell_run();
}
