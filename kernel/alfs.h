#pragma once

#include <stdint.h>

/* ── AliFS — Aligned LBA File System ────────────────────────────────────────
 *
 * A simple flat filesystem for AliOS.  On-disk layout (all sectors = 512 B):
 *
 *   Sector 0        Superblock          (alfs_super_t)
 *   Sectors 1-3     Directory table     (48 × alfs_dirent_t = 1536 B)
 *   Sector 4+       File data           (contiguous, one extent per file)
 *
 * Files are stored contiguously; there is no FAT chain.  The directory is
 * fully loaded into a BSS cache at alfs_init() time so lookups are fast.
 *
 * Capacity:
 *   - Up to 48 files (limited by the 3-sector directory).
 *   - File names up to 19 characters (ALFS_NAME_MAX − 1).
 *   - Max file size: limited only by disk size and uint32_t addressing.
 */

#define ALFS_MAGIC      0x414C4653u   /* 'ALFS' little-endian */
#define ALFS_VERSION    1u
#define ALFS_NAME_MAX   20            /* including null terminator */
#define ALFS_DIR_START  1u            /* directory begins at this sector */
#define ALFS_DIR_SECTS  3u            /* sectors occupied by directory */
#define ALFS_DATA_START 4u            /* first data sector */
#define ALFS_MAX_FILES  48            /* ALFS_DIR_SECTS * (512/32) */

/* ── On-disk structures ──────────────────────────────────────────────────── */

/*
 * Superblock — lives in sector 0.  Must start with the magic number so the
 * driver can confirm a valid AliFS image before trusting any other fields.
 */
typedef struct {
    uint32_t magic;        /*   0  0x414C4653 'ALFS'        */
    uint32_t version;      /*   4  format version (= 1)     */
    uint32_t dir_start;    /*   8  first directory sector    */
    uint32_t dir_sects;    /*  12  directory sector count    */
    uint32_t data_start;   /*  16  first data sector         */
    uint32_t total_sects;  /*  20  total sectors on disk     */
    uint8_t  pad[488];     /*  24  reserved, zero            */
                           /* 512 bytes total                */
} __attribute__((packed)) alfs_super_t;

/*
 * Directory entry — 32 bytes, 16 entries per sector.
 * An entry is free when flags bit 0 == 0.
 */
typedef struct {
    char     name[ALFS_NAME_MAX]; /*  0  null-terminated filename  */
    uint32_t start;               /* 20  first data sector         */
    uint32_t size;                /* 24  file size in bytes        */
    uint32_t flags;               /* 28  bit 0: 1=used, 0=free     */
                                  /* 32 bytes total                */
} __attribute__((packed)) alfs_dirent_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * alfs_init — read the superblock and directory from the ATA slave disk.
 *
 * Must be called after ata_init().  Safe to call when no disk is present;
 * all subsequent calls will return -1 / 0.
 *
 * Returns 0 on success, -1 if no disk, read error, or bad magic.
 */
int alfs_init(void);

/*
 * alfs_find — look up a file by name in the cached directory.
 *
 * Returns the entry index (0-based) on success, or -1 if not found.
 */
int alfs_find(const char *name);

/*
 * alfs_size — return the size (in bytes) of the file at entry index idx.
 *
 * Returns 0 if idx is out of range or the entry is free.
 */
uint32_t alfs_size(int idx);

/*
 * alfs_read — read up to max bytes of file idx into buf.
 *
 * buf must be at least max bytes.  Reads stop at EOF if size < max.
 * Returns the number of bytes copied on success, or -1 on error.
 */
int alfs_read(int idx, void *buf, uint32_t max);

/*
 * alfs_stat — query a directory slot by index.
 *
 * If slot idx is a used file, copies the null-terminated name into name_out
 * (caller must provide ALFS_NAME_MAX bytes), sets *size_out, and returns 1.
 * Returns 0 if the slot is free, idx is out of range, or FS is not ready.
 *
 * Iterate idx from 0 to ALFS_MAX_FILES-1 to list all files (for ls).
 */
int alfs_stat(int idx, char name_out[ALFS_NAME_MAX], uint32_t *size_out);

/*
 * alfs_pread — read len bytes from file idx at byte offset off into buf.
 *
 * Random-access counterpart to alfs_read; needed by the ELF loader to
 * reach segment data at arbitrary file positions.
 * Returns bytes read, or -1 on error.
 */
int alfs_pread(int idx, void *buf, uint32_t len, uint32_t off);

/*
 * alfs_write — create or replace a file named `name` with `size` bytes from `buf`.
 *
 * If a file with that name already exists its directory slot is reused but
 * new data is always appended past the current high-water mark (the old
 * sectors are abandoned — a limitation of the simple flat layout).
 * Returns the new entry index on success, or -1 on error (FS not ready,
 * directory full, disk write error).
 */
int alfs_write(const char *name, const void *buf, uint32_t size);

/*
 * alfs_delete — remove the file named `name` from the directory.
 *
 * The directory slot is marked free and flushed to disk.  The data sectors
 * are not reclaimed (space is leaked until the disk image is regenerated).
 * Returns 0 on success, -1 if the file was not found or the FS is not ready.
 */
int alfs_delete(const char *name);
