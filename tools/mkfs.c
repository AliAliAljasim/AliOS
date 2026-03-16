/*
 * mkfs — AliFS image creator
 *
 * Creates a raw AliFS disk image suitable for use as a QEMU virtual disk.
 *
 * Usage:
 *   mkfs <output.img> [hostpath:fsname ...]
 *
 * Examples:
 *   mkfs build/disk.img                            # empty filesystem
 *   mkfs build/disk.img build/hello.txt:hello.txt  # one file
 *   mkfs build/disk.img prog.bin:prog  data.txt:readme.txt
 *
 * The colon separates the host path from the in-filesystem name.
 * If no colon is present the basename of the host path is used as the
 * filesystem name.
 *
 * Disk layout produced:
 *   Sector 0        Superblock
 *   Sectors 1-3     Directory (48 entries × 32 bytes = 1536 bytes)
 *   Sector 4+       File data, contiguous, one extent per file
 *
 * The output image is always DISK_SECTS × 512 bytes (default 128 sectors =
 * 64 KB).  Increase DISK_SECTS if you need to store larger files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Must match kernel/alfs.h exactly ───────────────────────────────────── */
#define ALFS_MAGIC      0x414C4653u
#define ALFS_VERSION    1u
#define ALFS_DIR_START  1u
#define ALFS_DIR_SECTS  3u
#define ALFS_DATA_START 4u
#define ALFS_NAME_MAX   20
#define ALFS_MAX_FILES  48           /* ALFS_DIR_SECTS * (512 / 32) */

#define DISK_SECTS      128          /* 64 KB image */

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
    char     name[ALFS_NAME_MAX];
    uint32_t start;
    uint32_t size;
    uint32_t flags;
} __attribute__((packed)) alfs_dirent_t;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Return the last path component (basename) without modifying the original. */
static const char *basename_of(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            last = p + 1;
    return last;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: mkfs <output.img> [hostpath:fsname ...]\n");
        return 1;
    }

    int nfiles = argc - 2;
    if (nfiles > ALFS_MAX_FILES) {
        fprintf(stderr, "mkfs: too many files (max %d)\n", ALFS_MAX_FILES);
        return 1;
    }

    /* ── Allocate and zero the disk image ─────────────────────────────────── */
    uint8_t *disk = calloc(DISK_SECTS, 512);
    if (!disk) { perror("calloc"); return 1; }

    /* ── Write superblock (sector 0) ──────────────────────────────────────── */
    alfs_super_t *sb = (alfs_super_t *)(disk + 0);
    sb->magic       = ALFS_MAGIC;
    sb->version     = ALFS_VERSION;
    sb->dir_start   = ALFS_DIR_START;
    sb->dir_sects   = ALFS_DIR_SECTS;
    sb->data_start  = ALFS_DATA_START;
    sb->total_sects = DISK_SECTS;

    /* ── Directory pointer (sectors 1-3) ──────────────────────────────────── */
    alfs_dirent_t *dir = (alfs_dirent_t *)(disk + 512 * ALFS_DIR_START);

    /* ── Place each file into the data area ───────────────────────────────── */
    uint32_t next_sect = ALFS_DATA_START;
    int      written   = 0;

    for (int i = 0; i < nfiles; i++) {
        /* Split "hostpath:fsname" */
        char arg[512];
        strncpy(arg, argv[i + 2], sizeof(arg) - 1);
        arg[sizeof(arg) - 1] = '\0';

        char *colon = strchr(arg, ':');
        const char *hostpath, *fsname;
        if (colon) {
            *colon   = '\0';
            hostpath = arg;
            fsname   = colon + 1;
        } else {
            hostpath = arg;
            fsname   = basename_of(arg);
        }

        /* Validate filesystem name length */
        if (strlen(fsname) >= ALFS_NAME_MAX) {
            fprintf(stderr, "mkfs: name '%s' too long (max %d chars)\n",
                    fsname, ALFS_NAME_MAX - 1);
            free(disk);
            return 1;
        }

        /* Open and read the host file */
        FILE *f = fopen(hostpath, "rb");
        if (!f) {
            fprintf(stderr, "mkfs: cannot open '%s': ", hostpath);
            perror("");
            free(disk);
            return 1;
        }

        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint32_t sects_needed = (uint32_t)((fsize + 511) / 512);

        if (next_sect + sects_needed > DISK_SECTS) {
            fprintf(stderr, "mkfs: disk full — cannot fit '%s'\n", hostpath);
            fclose(f);
            free(disk);
            return 1;
        }

        /* Copy file data into the image */
        uint8_t *data_ptr = disk + 512 * next_sect;
        size_t   nread    = fread(data_ptr, 1, (size_t)fsize, f);
        fclose(f);

        if ((long)nread != fsize) {
            fprintf(stderr, "mkfs: short read from '%s'\n", hostpath);
            free(disk);
            return 1;
        }

        /* Write directory entry */
        strncpy(dir[written].name, fsname, ALFS_NAME_MAX - 1);
        dir[written].name[ALFS_NAME_MAX - 1] = '\0';
        dir[written].start = next_sect;
        dir[written].size  = (uint32_t)fsize;
        dir[written].flags = 1;   /* used */

        printf("mkfs: [%2d] %-19s  start=%-4u  size=%lu bytes\n",
               written, fsname, next_sect, (unsigned long)fsize);

        next_sect += sects_needed;
        written++;
    }

    /* ── Write output image ───────────────────────────────────────────────── */
    FILE *out = fopen(argv[1], "wb");
    if (!out) {
        fprintf(stderr, "mkfs: cannot create '%s': ", argv[1]);
        perror("");
        free(disk);
        return 1;
    }

    if (fwrite(disk, 512, DISK_SECTS, out) != DISK_SECTS) {
        fprintf(stderr, "mkfs: write error\n");
        fclose(out);
        free(disk);
        return 1;
    }

    fclose(out);
    free(disk);

    printf("mkfs: wrote %d file(s) → %s  (%d sectors, %d KB)\n",
           written, argv[1], DISK_SECTS, DISK_SECTS / 2);
    return 0;
}
