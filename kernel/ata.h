#pragma once

#include <stdint.h>

/*
 * ATA/IDE PIO driver — primary channel only (0x1F0-0x1F7 / IRQ14).
 *
 * Uses 28-bit LBA (Logical Block Addressing) which supports up to 128 GB.
 * All I/O is polled (busy-wait); no DMA, no interrupts needed.
 *
 * Drive numbers:
 *   ATA_MASTER (0) — primary master, usually the boot disk
 *   ATA_SLAVE  (1) — primary slave,  used for a data disk
 *
 * Sector size is always 512 bytes.  `buf` for read/write must be at least
 * `count * 512` bytes.
 */

#define ATA_MASTER  0
#define ATA_SLAVE   1

/*
 * ata_init — software-reset the primary ATA channel.
 *
 * Safe to call when no disk is present; read/write will return -1 in that
 * case.  Call once after heap_init() and before the first disk access.
 */
void ata_init(void);

/*
 * ata_read — read `count` sectors starting at `lba` from `drive` into `buf`.
 *
 * Returns 0 on success, -1 on error (drive absent, timeout, or ATA error).
 */
int ata_read(uint8_t drive, uint32_t lba, uint8_t count, void *buf);

/*
 * ata_write — write `count` sectors from `buf` to `drive` at `lba`.
 *
 * Issues a FLUSH WRITE CACHE command after writing to ensure data reaches
 * the medium.  Returns 0 on success, -1 on error.
 */
int ata_write(uint8_t drive, uint32_t lba, uint8_t count, const void *buf);
