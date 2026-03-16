#include "alfs.h"
#include "ata.h"
#include <stdint.h>

/* ── Internal state ─────────────────────────────────────────────────────── */

#define DRIVE  ATA_SLAVE   /* filesystem lives on the secondary drive */

/*
 * Both the superblock and the full directory table are cached in BSS at
 * alfs_init() time.  This costs ~2 KB of kernel memory but keeps all
 * metadata lookups fast (no disk I/O after init).
 */
static int           fs_ready = 0;
static alfs_super_t  fs_super;
static alfs_dirent_t fs_dir[ALFS_MAX_FILES];   /* 48 × 32 = 1536 bytes */
static uint8_t       sbuf[512];                /* shared sector bounce buffer */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/*
 * streq — compare two null-terminated strings for equality.
 * Returns 1 if identical, 0 otherwise.  Avoids a strcmp dependency.
 */
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * alfs_init — read the superblock and directory from disk, verify the magic.
 *
 * The directory fits in exactly ALFS_DIR_SECTS sectors, so we can read it
 * with a single multi-sector ATA command directly into the fs_dir array
 * (which is 1536 bytes == 3 × 512 bytes, exactly right).
 */
int alfs_init(void)
{
    /* Read and validate superblock. */
    if (ata_read(DRIVE, 0, 1, &fs_super) < 0)
        return -1;

    if (fs_super.magic != ALFS_MAGIC)
        return -1;   /* not an AliFS volume */

    /* Cache the entire directory table in one read. */
    if (ata_read(DRIVE, ALFS_DIR_START, ALFS_DIR_SECTS, fs_dir) < 0)
        return -1;

    fs_ready = 1;
    return 0;
}

/*
 * alfs_find — scan the cached directory for a matching filename.
 */
int alfs_find(const char *name)
{
    if (!fs_ready || !name)
        return -1;

    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if ((fs_dir[i].flags & 1) && streq(fs_dir[i].name, name))
            return i;
    }
    return -1;
}

/*
 * alfs_size — return the byte size of entry idx.
 */
uint32_t alfs_size(int idx)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES)
        return 0;
    if (!(fs_dir[idx].flags & 1))
        return 0;
    return fs_dir[idx].size;
}

/*
 * alfs_read — copy up to max bytes from file idx into buf.
 *
 * Reads the file one sector at a time into a static bounce buffer, then
 * copies the relevant portion into the caller's buffer.  This avoids
 * requiring the caller's buffer to be sector-aligned or sized.
 *
 * A static buffer is safe here because the kernel is single-threaded during
 * the init/demo phase; no concurrent filesystem access occurs.
 */
int alfs_read(int idx, void *buf, uint32_t max)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES)
        return -1;

    alfs_dirent_t *e = &fs_dir[idx];
    if (!(e->flags & 1))
        return -1;

    /* Clamp to the smaller of the file size and the caller's buffer. */
    uint32_t bytes = e->size < max ? e->size : max;
    if (!bytes)
        return 0;

    uint8_t       *dst       = (uint8_t *)buf;
    uint32_t       remaining  = bytes;
    uint32_t       sect       = e->start;

    while (remaining > 0) {
        if (ata_read(DRIVE, sect, 1, sbuf) < 0)
            return -1;

        uint32_t chunk = remaining < 512u ? remaining : 512u;

        /* memcpy equivalent — avoids pulling in an external header */
        for (uint32_t i = 0; i < chunk; i++)
            dst[i] = sbuf[i];

        dst       += chunk;
        remaining -= chunk;
        sect++;
    }

    return (int)bytes;
}

/*
 * alfs_stat — expose a cached directory entry to callers (e.g. shell ls).
 */
int alfs_stat(int idx, char name_out[ALFS_NAME_MAX], uint32_t *size_out)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES)
        return 0;
    if (!(fs_dir[idx].flags & 1))
        return 0;
    for (int i = 0; i < ALFS_NAME_MAX; i++)
        name_out[i] = fs_dir[idx].name[i];
    *size_out = fs_dir[idx].size;
    return 1;
}

/* ── Write helpers ───────────────────────────────────────────────────────── */

/*
 * dir_flush — write the cached directory back to the ATA slave disk.
 * Returns 0 on success, -1 on error.
 */
static int dir_flush(void)
{
    return ata_write(DRIVE, ALFS_DIR_START, ALFS_DIR_SECTS, fs_dir);
}

/*
 * high_water — return the first free data sector (i.e. the LBA just past the
 * last byte of every in-use file).  New files are appended here.
 */
static uint32_t high_water(void)
{
    uint32_t hwm = ALFS_DATA_START;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if (!(fs_dir[i].flags & 1)) continue;
        uint32_t end = fs_dir[i].start +
                       (fs_dir[i].size + 511u) / 512u;
        if (end > hwm) hwm = end;
    }
    return hwm;
}

/*
 * strncpy_name — copy src into a ALFS_NAME_MAX-byte name field, null-padded.
 */
static void strncpy_name(char *dst, const char *src)
{
    int i = 0;
    while (i < ALFS_NAME_MAX - 1 && src[i]) { dst[i] = src[i]; i++; }
    while (i < ALFS_NAME_MAX)  { dst[i] = '\0'; i++; }
}

/*
 * alfs_write — create or replace a file.
 */
int alfs_write(const char *name, const void *buf, uint32_t size)
{
    if (!fs_ready || !name || (!buf && size > 0))
        return -1;

    /* Find an existing entry to reuse, or a free slot. */
    int slot = -1;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if ((fs_dir[i].flags & 1) && streq(fs_dir[i].name, name)) {
            /* Existing file: mark free so high_water ignores it below. */
            fs_dir[i].flags = 0;
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < ALFS_MAX_FILES; i++) {
            if (!(fs_dir[i].flags & 1)) { slot = i; break; }
        }
    }
    if (slot < 0) return -1;   /* directory full */

    uint32_t start = high_water();

    /* Write data sector by sector, zero-padding the final partial sector. */
    const uint8_t *src       = (const uint8_t *)buf;
    uint32_t       remaining  = size;
    uint32_t       lba        = start;

    while (remaining > 0 || (lba == start && size == 0)) {
        uint32_t chunk = remaining < 512u ? remaining : 512u;

        /* Build sector: copy file data then zero-pad. */
        for (uint32_t i = 0; i < chunk; i++)
            sbuf[i] = src[i];
        for (uint32_t i = chunk; i < 512u; i++)
            sbuf[i] = 0;

        if (ata_write(DRIVE, lba, 1, sbuf) < 0)
            return -1;

        src       += chunk;
        remaining -= chunk;
        lba++;

        if (size == 0) break;   /* empty file: wrote one zero sector */
    }

    /* Update the directory entry and flush. */
    strncpy_name(fs_dir[slot].name, name);
    fs_dir[slot].start = start;
    fs_dir[slot].size  = size;
    fs_dir[slot].flags = 1;

    if (dir_flush() < 0) return -1;
    return slot;
}

/*
 * alfs_delete — mark a directory entry free and flush to disk.
 */
int alfs_delete(const char *name)
{
    if (!fs_ready || !name) return -1;

    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if ((fs_dir[i].flags & 1) && streq(fs_dir[i].name, name)) {
            fs_dir[i].flags = 0;
            return dir_flush();
        }
    }
    return -1;   /* not found */
}

/*
 * alfs_pread — copy len bytes from file idx starting at byte offset off.
 *
 * Unlike alfs_read, this allows reading from an arbitrary offset, which is
 * necessary for ELF loading (segments start at arbitrary file positions).
 * Shares the module-level sbuf; safe because the kernel is single-threaded
 * during loading.
 */
int alfs_pread(int idx, void *buf, uint32_t len, uint32_t off)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES)
        return -1;

    alfs_dirent_t *e = &fs_dir[idx];
    if (!(e->flags & 1))
        return -1;

    /* Nothing to read past EOF. */
    if (off >= e->size)
        return 0;

    uint32_t avail = e->size - off;
    uint32_t bytes = len < avail ? len : avail;
    if (!bytes)
        return 0;

    uint8_t  *dst      = (uint8_t *)buf;
    uint32_t  remaining = bytes;
    uint32_t  file_off  = off;

    while (remaining > 0) {
        uint32_t sect_idx = file_off / 512;
        uint32_t sect_off = file_off % 512;
        uint32_t chunk    = 512u - sect_off;
        if (chunk > remaining) chunk = remaining;

        if (ata_read(DRIVE, e->start + sect_idx, 1, sbuf) < 0)
            return -1;

        for (uint32_t i = 0; i < chunk; i++)
            dst[i] = sbuf[sect_off + i];

        dst       += chunk;
        file_off  += chunk;
        remaining -= chunk;
    }

    return (int)bytes;
}
