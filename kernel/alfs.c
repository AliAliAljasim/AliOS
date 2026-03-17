#include "alfs.h"
#include "ata.h"
#include <stdint.h>

#define DRIVE  ATA_SLAVE

static int           fs_ready = 0;
static alfs_super_t  fs_super;
static alfs_dirent_t fs_dir[ALFS_MAX_FILES];   /* 48 × 64 = 3072 bytes */
static uint8_t       sbuf[512];

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void strncpy_name(char *dst, const char *src)
{
    int i = 0;
    while (i < ALFS_NAME_MAX - 1 && src[i]) { dst[i] = src[i]; i++; }
    while (i < ALFS_NAME_MAX) { dst[i] = '\0'; i++; }
}

int alfs_init(void)
{
    if (ata_read(DRIVE, 0, 1, &fs_super) < 0) return -1;
    if (fs_super.magic != ALFS_MAGIC)          return -1;

    if (ata_read(DRIVE, ALFS_DIR_START, ALFS_DIR_SECTS, fs_dir) < 0)
        return -1;

    fs_ready = 1;
    return 0;
}

int alfs_find(const char *name)
{
    if (!fs_ready || !name) return -1;
    for (int i = 0; i < ALFS_MAX_FILES; i++)
        if ((fs_dir[i].flags & ALFS_FLAG_USED) && streq(fs_dir[i].name, name))
            return i;
    return -1;
}

uint32_t alfs_size(int idx)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES) return 0;
    if (!(fs_dir[idx].flags & ALFS_FLAG_USED)) return 0;
    return fs_dir[idx].size;
}

int alfs_is_dir(int idx)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES) return 0;
    return (fs_dir[idx].flags & ALFS_FLAG_DIR) != 0;
}

int alfs_read(int idx, void *buf, uint32_t max)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES) return -1;
    alfs_dirent_t *e = &fs_dir[idx];
    if (!(e->flags & ALFS_FLAG_USED)) return -1;

    uint32_t bytes = e->size < max ? e->size : max;
    if (!bytes) return 0;

    uint8_t *dst        = (uint8_t *)buf;
    uint32_t full_sects = bytes / 512;
    uint32_t partial    = bytes % 512;
    uint32_t lba        = e->start;

    /* Read all complete sectors directly into dst — one ATA call per 255 sectors. */
    uint32_t done = 0;
    while (done < full_sects) {
        uint8_t batch = (full_sects - done > 255u) ? 255u : (uint8_t)(full_sects - done);
        if (ata_read(DRIVE, lba + done, batch, dst + done * 512u) < 0) return -1;
        done += batch;
    }

    /* Read the final partial sector through the bounce buffer. */
    if (partial) {
        if (ata_read(DRIVE, lba + full_sects, 1, sbuf) < 0) return -1;
        for (uint32_t i = 0; i < partial; i++) dst[full_sects * 512u + i] = sbuf[i];
    }

    return (int)bytes;
}

int alfs_stat(int idx, char name_out[ALFS_NAME_MAX], uint32_t *size_out)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES) return 0;
    if (!(fs_dir[idx].flags & ALFS_FLAG_USED)) return 0;
    for (int i = 0; i < ALFS_NAME_MAX; i++) name_out[i] = fs_dir[idx].name[i];
    *size_out = fs_dir[idx].size;
    return 1;
}

/* dir_flush_slot — write only the single 512-byte sector that contains entry idx.
 * This reduces metadata writes from 6 sectors to 1 sector per change. */
static int dir_flush_slot(int idx)
{
    /* 8 entries per 512-byte sector (64 bytes each). */
    uint32_t sector_within_dir = (uint32_t)idx / 8;
    uint32_t lba = ALFS_DIR_START + sector_within_dir;
    return ata_write(DRIVE, lba, 1,
                     (const uint8_t *)fs_dir + sector_within_dir * 512u);
}

static int dir_flush(void)
{
    return ata_write(DRIVE, ALFS_DIR_START, ALFS_DIR_SECTS, fs_dir);
}

static uint32_t high_water(void)
{
    uint32_t hwm = ALFS_DATA_START;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if (!(fs_dir[i].flags & ALFS_FLAG_USED)) continue;
        if (fs_dir[i].flags & ALFS_FLAG_DIR)     continue;  /* dirs have no data */
        uint32_t end = fs_dir[i].start + (fs_dir[i].size + 511u) / 512u;
        if (end > hwm) hwm = end;
    }
    return hwm;
}

int alfs_write(const char *name, const void *buf, uint32_t size)
{
    if (!fs_ready || !name || (!buf && size > 0)) return -1;

    int slot = -1;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if ((fs_dir[i].flags & ALFS_FLAG_USED) && streq(fs_dir[i].name, name)) {
            if (fs_dir[i].flags & ALFS_FLAG_DIR) return -1;  /* can't overwrite dir */
            fs_dir[i].flags = 0;
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < ALFS_MAX_FILES; i++)
            if (!(fs_dir[i].flags & ALFS_FLAG_USED)) { slot = i; break; }
    }
    if (slot < 0) return -1;

    uint32_t start = high_water();

    const uint8_t *src        = (const uint8_t *)buf;
    uint32_t       full_sects = size / 512u;
    uint32_t       partial    = size % 512u;
    uint32_t       lba        = start;

    /* Write all complete sectors in one ATA command. */
    if (full_sects > 0) {
        uint32_t done = 0;
        while (done < full_sects) {
            uint8_t batch = (full_sects - done > 255u) ? 255u : (uint8_t)(full_sects - done);
            if (ata_write(DRIVE, lba + done, batch,
                          src + done * 512u) < 0) return -1;
            done += batch;
        }
    }

    /* Write the final partial sector (or one empty sector for a zero-byte file). */
    if (partial || size == 0) {
        uint32_t chunk = partial;
        for (uint32_t i = 0; i < chunk; i++) sbuf[i] = src[full_sects * 512u + i];
        for (uint32_t i = chunk; i < 512u; i++) sbuf[i] = 0;
        if (ata_write(DRIVE, lba + full_sects, 1, sbuf) < 0) return -1;
    }

    strncpy_name(fs_dir[slot].name, name);
    fs_dir[slot].start = start;
    fs_dir[slot].size  = size;
    fs_dir[slot].flags = ALFS_FLAG_USED;

    if (dir_flush_slot(slot) < 0) return -1;
    return slot;
}

int alfs_mkdir(const char *path)
{
    if (!fs_ready || !path) return -1;
    if (alfs_find(path) >= 0) return -1;   /* already exists */

    int slot = -1;
    for (int i = 0; i < ALFS_MAX_FILES; i++)
        if (!(fs_dir[i].flags & ALFS_FLAG_USED)) { slot = i; break; }
    if (slot < 0) return -1;

    strncpy_name(fs_dir[slot].name, path);
    fs_dir[slot].start = 0;
    fs_dir[slot].size  = 0;
    fs_dir[slot].flags = ALFS_FLAG_USED | ALFS_FLAG_DIR;

    if (dir_flush_slot(slot) < 0) return -1;
    return slot;
}

int alfs_delete(const char *name)
{
    if (!fs_ready || !name) return -1;
    for (int i = 0; i < ALFS_MAX_FILES; i++) {
        if ((fs_dir[i].flags & ALFS_FLAG_USED) && streq(fs_dir[i].name, name)) {
            fs_dir[i].flags = 0;
            return dir_flush_slot(i);
        }
    }
    return -1;
}

int alfs_pread(int idx, void *buf, uint32_t len, uint32_t off)
{
    if (!fs_ready || idx < 0 || idx >= ALFS_MAX_FILES) return -1;
    alfs_dirent_t *e = &fs_dir[idx];
    if (!(e->flags & ALFS_FLAG_USED)) return -1;

    if (off >= e->size) return 0;
    uint32_t avail = e->size - off;
    uint32_t bytes = len < avail ? len : avail;
    if (!bytes) return 0;

    uint8_t  *dst       = (uint8_t *)buf;
    uint32_t  remaining = bytes;
    uint32_t  file_off  = off;

    while (remaining > 0) {
        uint32_t sect_idx = file_off / 512;
        uint32_t sect_off = file_off % 512;

        if (sect_off == 0 && remaining >= 512u) {
            /* Aligned, multi-sector: read as many full sectors as possible. */
            uint32_t full = remaining / 512u;
            if (full > 255u) full = 255u;
            if (ata_read(DRIVE, e->start + sect_idx, (uint8_t)full, dst) < 0) return -1;
            uint32_t got = full * 512u;
            dst += got; file_off += got; remaining -= got;
        } else {
            /* Unaligned or partial sector — use the bounce buffer. */
            uint32_t chunk = 512u - sect_off;
            if (chunk > remaining) chunk = remaining;
            if (ata_read(DRIVE, e->start + sect_idx, 1, sbuf) < 0) return -1;
            for (uint32_t i = 0; i < chunk; i++) dst[i] = sbuf[sect_off + i];
            dst += chunk; file_off += chunk; remaining -= chunk;
        }
    }
    return (int)bytes;
}
