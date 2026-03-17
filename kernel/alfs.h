#pragma once

#include <stdint.h>

/* ── AliFS — Aligned LBA File System ────────────────────────────────────────
 *
 * A flat filesystem with directory entries for AliOS.
 *
 * On-disk layout (all sectors = 512 B):
 *
 *   Sector 0        Superblock          (alfs_super_t, 512 B)
 *   Sectors 1-6     Directory table     (48 × 64 B = 3072 B)
 *   Sector 7+       File data           (contiguous, one extent per file)
 *
 * Each directory entry is 64 bytes:
 *   name[52]   — absolute path, e.g. "/bin/echo" (null-terminated)
 *   start      — first data sector (0 for directories)
 *   size       — file size in bytes (0 for directories)
 *   flags      — bit 0: in-use; bit 1: is-directory
 *
 * Path convention:
 *   All stored paths are absolute and start with '/'.
 *   Root "/" itself is not stored as an entry.
 *   Directories are stored as entries: name="/bin", flags=3.
 *   Files are stored as entries:       name="/bin/echo", flags=1.
 */

#define ALFS_MAGIC          0x414C4653u   /* 'ALFS' */
#define ALFS_VERSION        2u            /* bumped for 64-byte entry format */
#define ALFS_NAME_MAX       52            /* including null terminator */
#define ALFS_DIR_START      1u
#define ALFS_DIR_SECTS      6u            /* 6 × 512 / 64 = 48 entries */
#define ALFS_DATA_START     7u
#define ALFS_MAX_FILES      48            /* ALFS_DIR_SECTS × (512/64) */

/* flags field bits */
#define ALFS_FLAG_USED      1u            /* entry is in use */
#define ALFS_FLAG_DIR       2u            /* entry is a directory */

/* ── On-disk structures ──────────────────────────────────────────────────── */

typedef struct {
    uint32_t magic;        /*   0  0x414C4653 'ALFS'        */
    uint32_t version;      /*   4  format version (= 2)     */
    uint32_t dir_start;    /*   8  first directory sector    */
    uint32_t dir_sects;    /*  12  directory sector count    */
    uint32_t data_start;   /*  16  first data sector         */
    uint32_t total_sects;  /*  20  total sectors on disk     */
    uint8_t  pad[488];     /*  24  reserved, zero            */
                           /* 512 bytes total                */
} __attribute__((packed)) alfs_super_t;

typedef struct {
    char     name[ALFS_NAME_MAX]; /*  0  absolute path, null-terminated    */
    uint32_t start;               /* 52  first data sector (0 for dirs)    */
    uint32_t size;                /* 56  file size in bytes (0 for dirs)   */
    uint32_t flags;               /* 60  ALFS_FLAG_USED | ALFS_FLAG_DIR    */
                                  /* 64 bytes total                        */
} __attribute__((packed)) alfs_dirent_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

int      alfs_init(void);
int      alfs_find(const char *name);
uint32_t alfs_size(int idx);
int      alfs_read(int idx, void *buf, uint32_t max);
int      alfs_pread(int idx, void *buf, uint32_t len, uint32_t off);
int      alfs_stat(int idx, char name_out[ALFS_NAME_MAX], uint32_t *size_out);
int      alfs_write(const char *name, const void *buf, uint32_t size);
int      alfs_delete(const char *name);

/* alfs_mkdir — create a directory entry.  Returns index or -1 on error. */
int      alfs_mkdir(const char *path);

/* alfs_is_dir — return 1 if entry idx is a directory, 0 otherwise. */
int      alfs_is_dir(int idx);
