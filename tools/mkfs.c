/*
 * mkfs — AliFS v2 image creator
 *
 * Usage:
 *   mkfs <output.img> [entry ...]
 *
 * Entry forms:
 *   hostpath:fspath      — copy host file to fspath in image
 *   d:fspath             — create a directory entry at fspath
 *
 * Examples:
 *   mkfs build/disk.img d:/bin d:/home \
 *        build/echo.elf:/bin/echo build/cat.elf:/bin/cat \
 *        user/greeting.txt:/home/greeting.txt
 *
 * Disk layout:
 *   Sector 0        Superblock
 *   Sectors 1-6     Directory (48 entries × 64 bytes = 3072 bytes)
 *   Sector 7+       File data
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Must match kernel/alfs.h exactly ───────────────────────────────────── */
#define ALFS_MAGIC      0x414C4653u
#define ALFS_VERSION    2u
#define ALFS_DIR_START  1u
#define ALFS_DIR_SECTS  6u
#define ALFS_DATA_START 7u
#define ALFS_NAME_MAX   52
#define ALFS_MAX_FILES  48           /* ALFS_DIR_SECTS × (512 / 64) */
#define ALFS_FLAG_USED  1u
#define ALFS_FLAG_DIR   2u

#define DISK_SECTS      256          /* 128 KB image — room for dirs + data */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t dir_start;
    uint32_t dir_sects;
    uint32_t data_start;
    uint32_t total_sects;
    uint8_t  pad[488];
} __attribute__((packed)) alfs_super_t;

typedef struct {
    char     name[ALFS_NAME_MAX];   /* 52 bytes */
    uint32_t start;                 /*  4 bytes */
    uint32_t size;                  /*  4 bytes */
    uint32_t flags;                 /*  4 bytes */
                                    /* 64 bytes total */
} __attribute__((packed)) alfs_dirent_t;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: mkfs <output.img> [d:path | hostpath:fspath ...]\n");
        return 1;
    }

    int nentries = argc - 2;
    if (nentries > ALFS_MAX_FILES) {
        fprintf(stderr, "mkfs: too many entries (max %d)\n", ALFS_MAX_FILES);
        return 1;
    }

    uint8_t *disk = calloc(DISK_SECTS, 512);
    if (!disk) { perror("calloc"); return 1; }

    /* Superblock */
    alfs_super_t *sb = (alfs_super_t *)(disk + 0);
    sb->magic       = ALFS_MAGIC;
    sb->version     = ALFS_VERSION;
    sb->dir_start   = ALFS_DIR_START;
    sb->dir_sects   = ALFS_DIR_SECTS;
    sb->data_start  = ALFS_DATA_START;
    sb->total_sects = DISK_SECTS;

    alfs_dirent_t *dir = (alfs_dirent_t *)(disk + 512 * ALFS_DIR_START);

    uint32_t next_sect = ALFS_DATA_START;
    int      written   = 0;

    for (int i = 0; i < nentries; i++) {
        char arg[512];
        strncpy(arg, argv[i + 2], sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';

        char *colon = strchr(arg, ':');
        if (!colon) {
            fprintf(stderr, "mkfs: missing ':' in '%s'\n", arg);
            free(disk); return 1;
        }
        *colon = '\0';
        const char *left  = arg;        /* host path, or "d" for directory */
        const char *fspath = colon + 1; /* filesystem absolute path */

        if (strlen(fspath) >= ALFS_NAME_MAX) {
            fprintf(stderr, "mkfs: path '%s' too long (max %d)\n",
                    fspath, ALFS_NAME_MAX - 1);
            free(disk); return 1;
        }

        /* Directory entry */
        if (left[0] == 'd' && left[1] == '\0') {
            strncpy(dir[written].name, fspath, ALFS_NAME_MAX - 1);
            dir[written].name[ALFS_NAME_MAX - 1] = '\0';
            dir[written].start = 0;
            dir[written].size  = 0;
            dir[written].flags = ALFS_FLAG_USED | ALFS_FLAG_DIR;
            printf("mkfs: [%2d] %-30s  <dir>\n", written, fspath);
            written++;
            continue;
        }

        /* File entry */
        FILE *f = fopen(left, "rb");
        if (!f) {
            fprintf(stderr, "mkfs: cannot open '%s': ", left);
            perror(""); free(disk); return 1;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint32_t sects_needed = (uint32_t)((fsize + 511) / 512);
        if (next_sect + sects_needed > DISK_SECTS) {
            fprintf(stderr, "mkfs: disk full — cannot fit '%s'\n", left);
            fclose(f); free(disk); return 1;
        }

        uint8_t *data_ptr = disk + 512 * next_sect;
        size_t   nread    = fread(data_ptr, 1, (size_t)fsize, f);
        fclose(f);
        if ((long)nread != fsize) {
            fprintf(stderr, "mkfs: short read from '%s'\n", left);
            free(disk); return 1;
        }

        strncpy(dir[written].name, fspath, ALFS_NAME_MAX - 1);
        dir[written].name[ALFS_NAME_MAX - 1] = '\0';
        dir[written].start = next_sect;
        dir[written].size  = (uint32_t)fsize;
        dir[written].flags = ALFS_FLAG_USED;

        printf("mkfs: [%2d] %-30s  start=%-4u  size=%lu bytes\n",
               written, fspath, next_sect, (unsigned long)fsize);

        next_sect += sects_needed;
        written++;
    }

    FILE *out = fopen(argv[1], "wb");
    if (!out) {
        fprintf(stderr, "mkfs: cannot create '%s': ", argv[1]);
        perror(""); free(disk); return 1;
    }
    if (fwrite(disk, 512, DISK_SECTS, out) != DISK_SECTS) {
        fprintf(stderr, "mkfs: write error\n");
        fclose(out); free(disk); return 1;
    }
    fclose(out);
    free(disk);

    printf("mkfs: wrote %d entr%s → %s  (%d sectors, %d KB)\n",
           written, written == 1 ? "y" : "ies",
           argv[1], DISK_SECTS, DISK_SECTS / 2);
    return 0;
}
