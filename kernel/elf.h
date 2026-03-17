#pragma once

#include <stdint.h>

/* ── ELF32 loader ────────────────────────────────────────────────────────────
 *
 * Loads an ELF32 ET_EXEC executable stored in AliFS into a user address space.
 *
 * Only PT_LOAD program headers are processed.  Each page of every PT_LOAD
 * segment is allocated from the PMM, zeroed (covering BSS implicitly), filled
 * with the corresponding file bytes via alfs_pread, and mapped into the
 * caller-supplied page directory with appropriate user/write flags.
 *
 * The caller is responsible for:
 *   - Creating the page directory (uvm_create) before calling elf_load.
 *   - Allocating and mapping a user stack after elf_load returns.
 *   - Creating the task (task_create_user) and scheduling it.
 */

/* ── ELF32 on-disk types ─────────────────────────────────────────────────── */

#define ELF_MAGIC    0x464C457Fu   /* 0x7F 'E' 'L' 'F' as little-endian u32 */
#define ELFCLASS32   1u
#define ELFDATA2LSB  1u            /* little-endian */
#define ET_EXEC      2u
#define EM_386       3u
#define PT_LOAD      1u
#define PF_X         1u
#define PF_W         2u
#define PF_R         4u

/* ELF executable header — 52 bytes */
typedef struct {
    uint8_t  e_ident[16];  /*  0  magic, class, data-encoding, version, OS/ABI */
    uint16_t e_type;       /* 16  ET_EXEC = 2                                   */
    uint16_t e_machine;    /* 18  EM_386  = 3                                   */
    uint32_t e_version;    /* 20  always 1                                      */
    uint32_t e_entry;      /* 24  virtual entry-point address                   */
    uint32_t e_phoff;      /* 28  file offset to program-header table           */
    uint32_t e_shoff;      /* 32  file offset to section-header table (unused)  */
    uint32_t e_flags;      /* 36  architecture flags (usually 0 for i386)       */
    uint16_t e_ehsize;     /* 40  ELF header size (= 52)                        */
    uint16_t e_phentsize;  /* 42  size of one program header (= 32)             */
    uint16_t e_phnum;      /* 44  number of program headers                     */
    uint16_t e_shentsize;  /* 46  size of one section header                    */
    uint16_t e_shnum;      /* 48  number of section headers                     */
    uint16_t e_shstrndx;   /* 50  section index of .shstrtab                    */
} __attribute__((packed)) Elf32_Ehdr;

/* ELF32 program header — 32 bytes */
typedef struct {
    uint32_t p_type;    /*  0  PT_LOAD = 1, PT_NULL = 0, etc.         */
    uint32_t p_offset;  /*  4  file offset of segment data             */
    uint32_t p_vaddr;   /*  8  virtual address to load segment at      */
    uint32_t p_paddr;   /* 12  physical address (ignored)              */
    uint32_t p_filesz;  /* 16  bytes to read from file                 */
    uint32_t p_memsz;   /* 20  bytes to reserve in virtual memory      */
    uint32_t p_flags;   /* 24  PF_R | PF_W | PF_X                     */
    uint32_t p_align;   /* 28  alignment (usually 0x1000 = page-align) */
} __attribute__((packed)) Elf32_Phdr;

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * elf_load — parse and load an ELF32 executable from AliFS.
 *
 *   pd         — user page directory created with uvm_create().
 *   filename   — AliFS file name (e.g. "hello").
 *   entry_out  — set to e_entry on success.
 *
 * Returns 0 on success, -1 on any error (file not found, bad ELF, OOM).
 *
 * On success the caller maps a stack, creates a user task at *entry_out,
 * and calls sched_add().  On failure the caller should free pd with uvm_free().
 */
int elf_load(uint32_t *pd, const char *filename, uint32_t *entry_out);

/*
 * elf_push_args — write argc/argv into a user stack page; return new ESP.
 *
 *   stack_page  — physical (== virtual via identity map) address of the 4 KB page.
 *   argc        — number of argument strings (argv[0] = program name).
 *   argv        — kernel-space string pointers; the strings are copied onto the page.
 *
 * The page must already be zeroed.  Returns the virtual ESP the task should start at.
 * Stack layout seen by  void _start(int argc, char **argv):
 *   [esp+0] fake return address (0)   [esp+4] argc   [esp+8] argv pointer
 */
uint32_t elf_push_args(uint8_t *stack_page, int argc, const char **argv);
