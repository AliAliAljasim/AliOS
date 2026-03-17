#include "elf.h"
#include "alfs.h"
#include "pmm.h"
#include "paging.h"
#include "uvm.h"
#include <stdint.h>

/* ── Constants ───────────────────────────────────────────────────────────── */

#define PAGE_SIZE  4096u
#define PAGE_MASK  (PAGE_SIZE - 1u)
#define MAX_PHDR   8u   /* maximum program headers we will process */

/* ── Module-level static buffers ─────────────────────────────────────────── */

/*
 * Two small buffers live in BSS.  Using statics avoids stack pressure and
 * keeps the loader self-contained.  Safe because elf_load is only called
 * from the single-threaded kernel init path.
 */
static uint8_t   hdr_buf[512];            /* first sector: ELF header + phdrs */
static Elf32_Phdr ph_buf[MAX_PHDR];       /* program header cache              */

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Zero PAGE_SIZE bytes via the identity map (phys == virt for PMM pages). */
static void zero_page(uint32_t phys)
{
    uint8_t *p = (uint8_t *)phys;
    for (uint32_t i = 0; i < PAGE_SIZE; i++)
        p[i] = 0;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int elf_load(uint32_t *pd, const char *filename, uint32_t *entry_out)
{
    /* ── 1. Find the file in AliFS ──────────────────────────────────────── */
    int idx = alfs_find(filename);
    if (idx < 0)
        return -1;

    /* ── 2. Read the first 512 bytes — contains the ELF header ─────────── */
    if (alfs_pread(idx, hdr_buf, sizeof(hdr_buf), 0) < 0)
        return -1;

    Elf32_Ehdr *eh = (Elf32_Ehdr *)hdr_buf;

    /* ── 3. Validate header ─────────────────────────────────────────────── */
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F')
        return -1;   /* bad magic */

    if (eh->e_ident[4] != ELFCLASS32)   return -1;   /* not 32-bit       */
    if (eh->e_type      != ET_EXEC)     return -1;   /* not executable    */
    if (eh->e_machine   != EM_386)      return -1;   /* not x86           */
    if (eh->e_phentsize != sizeof(Elf32_Phdr)) return -1;
    if (eh->e_phnum == 0 || eh->e_phnum > MAX_PHDR)  return -1;

    uint32_t entry = eh->e_entry;
    uint32_t phoff = eh->e_phoff;
    uint16_t phnum = eh->e_phnum;

    /* ── 4. Read program headers ────────────────────────────────────────── */
    /*
     * For simple programs the phdrs sit immediately after the ELF header
     * (phoff = 52) and fit within the first 512-byte sector we already
     * read.  For robustness we use alfs_pread regardless.
     */
    if (alfs_pread(idx, ph_buf, phnum * sizeof(Elf32_Phdr), phoff) < 0)
        return -1;

    /* ── 5. Map each PT_LOAD segment ────────────────────────────────────── */
    for (uint16_t i = 0; i < phnum; i++) {
        Elf32_Phdr *ph = &ph_buf[i];

        if (ph->p_type != PT_LOAD)  continue;
        if (ph->p_memsz == 0)       continue;

        /* Reject segments outside the safe user address range.
         * Identity map (< 0x08000000) and kernel high (>= 0xC0000000)
         * are supervisor-only and must never be mapped as user pages. */
        if (ph->p_vaddr < 0x08000000u || ph->p_vaddr >= 0xC0000000u)
            return -1;

        /* Page-align the virtual address range. */
        uint32_t va_page = ph->p_vaddr & ~PAGE_MASK;
        uint32_t va_end  = (ph->p_vaddr + ph->p_memsz + PAGE_MASK) & ~PAGE_MASK;

        /*
         * PTE flags: user-accessible always; writable only when PF_W is set.
         * x86 32-bit paging has no separate execute-disable bit without PAE/NX,
         * so executable-only segments are mapped read-only for ring 3.
         */
        uint32_t pte_flags = PAGE_USER;
        if (ph->p_flags & PF_W)
            pte_flags |= PAGE_WRITE;

        /* File data lives at [p_vaddr, p_vaddr + p_filesz) in virtual space,
           and at [p_offset, p_offset + p_filesz) in the file. */
        uint32_t file_va_lo = ph->p_vaddr;
        uint32_t file_va_hi = ph->p_vaddr + ph->p_filesz;

        for (uint32_t va = va_page; va < va_end; va += PAGE_SIZE) {
            /* Allocate a fresh physical page. */
            uint32_t phys = pmm_alloc();
            if (!phys)
                return -1;

            /* Zero the whole page — this handles the BSS portion for free. */
            zero_page(phys);

            /*
             * Copy any file bytes that overlap this page.
             *
             * Overlap: intersection of [va, va+PAGE_SIZE) and [file_va_lo, file_va_hi).
             */
            uint32_t copy_lo = va          > file_va_lo ? va          : file_va_lo;
            uint32_t copy_hi = va+PAGE_SIZE < file_va_hi ? va+PAGE_SIZE : file_va_hi;

            if (copy_lo < copy_hi) {
                uint32_t page_off = copy_lo - va;              /* offset into phys page */
                uint32_t file_off = ph->p_offset + (copy_lo - file_va_lo);
                uint32_t copy_len = copy_hi - copy_lo;

                if (alfs_pread(idx, (uint8_t *)phys + page_off,
                               copy_len, file_off) < 0)
                    return -1;
            }

            /* Map the page into the user address space. */
            if (uvm_map(pd, va, phys, pte_flags) < 0)
                return -1;
        }
    }

    *entry_out = entry;
    return 0;
}

/* ── elf_push_args ───────────────────────────────────────────────────────── */

#define USER_STACK_VBASE  0xBFFFF000u

uint32_t elf_push_args(uint8_t *stack_page, int argc, const char **argv)
{
    if (argc > 15) argc = 15;   /* hard cap */

    uint32_t str_vaddrs[15];
    int top = 4095;             /* index of last byte in the 4 KB page */

    /* 1. Copy strings from the top of the page downward. */
    for (int i = argc - 1; i >= 0; i--) {
        const char *s = argv[i];
        int len = 0;
        while (s[len]) len++;
        len++;                  /* include null terminator */
        top -= len;
        if (top < 64) { top += len; argc = i; break; }   /* out of space */
        for (int j = 0; j < len; j++)
            stack_page[top + j] = (uint8_t)s[j];
        str_vaddrs[i] = USER_STACK_VBASE + (uint32_t)top;
    }

    /* 2. Align down to 4 bytes. */
    top &= ~3;

    /* 3. Write NULL-terminated argv[] pointer array. */
    top -= 4;
    *(uint32_t *)(stack_page + top) = 0;              /* NULL terminator */
    for (int i = argc - 1; i >= 0; i--) {
        top -= 4;
        *(uint32_t *)(stack_page + top) = str_vaddrs[i];
    }
    uint32_t argv_vaddr = USER_STACK_VBASE + (uint32_t)top;

    /* 4. Push argv pointer, argc, and fake return address. */
    top -= 4; *(uint32_t *)(stack_page + top) = argv_vaddr;
    top -= 4; *(uint32_t *)(stack_page + top) = (uint32_t)argc;
    top -= 4; *(uint32_t *)(stack_page + top) = 0;     /* fake retaddr */

    return USER_STACK_VBASE + (uint32_t)top;           /* initial user ESP */
}
