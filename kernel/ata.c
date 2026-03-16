#include "ata.h"
#include "io.h"
#include <stdint.h>

/* ── Primary ATA channel I/O ports ──────────────────────────────────────────
 *
 * The ATA data register is 16 bits wide; all others are 8 bits.
 * Port 0x3F6 (Alternate Status / Device Control) can be read at any time
 * without side effects — useful as a 400 ns delay mechanism.
 */
#define ATA_DATA     0x1F0   /* Data register            (16-bit R/W) */
#define ATA_ERR      0x1F1   /* Error register           (8-bit  R)   */
#define ATA_FEAT     0x1F1   /* Features register        (8-bit  W)   */
#define ATA_COUNT    0x1F2   /* Sector count             (8-bit  R/W) */
#define ATA_LBA_LO   0x1F3   /* LBA bits  0-7            (8-bit  R/W) */
#define ATA_LBA_MID  0x1F4   /* LBA bits  8-15           (8-bit  R/W) */
#define ATA_LBA_HI   0x1F5   /* LBA bits 16-23           (8-bit  R/W) */
#define ATA_DRIVE    0x1F6   /* Drive/Head register      (8-bit  R/W) */
#define ATA_STATUS   0x1F7   /* Status register          (8-bit  R)   */
#define ATA_CMD      0x1F7   /* Command register         (8-bit  W)   */
#define ATA_CTL      0x3F6   /* Alt-status / Device ctrl (8-bit  R/W) */

/* ── Status register bit masks ────────────────────────────────────────────── */
#define ATA_SR_BSY   0x80    /* controller busy         */
#define ATA_SR_DRDY  0x40    /* drive ready             */
#define ATA_SR_DRQ   0x08    /* data request            */
#define ATA_SR_ERR   0x01    /* error                   */

/* ── ATA commands ────────────────────────────────────────────────────────── */
#define ATA_CMD_READ   0x20  /* READ SECTORS  (with retry)  */
#define ATA_CMD_WRITE  0x30  /* WRITE SECTORS (with retry)  */
#define ATA_CMD_FLUSH  0xE7  /* FLUSH WRITE CACHE           */

/* ── Internal helpers ────────────────────────────────────────────────────── */

/*
 * ata_delay400 — give the drive ~400 ns to update BSY.
 *
 * Reading the Alternate Status register four times is the standard technique:
 * each inb takes ≥ 100 ns on real hardware; in QEMU it's effectively instant
 * but the emulated drive still needs the delay to latch its BSY bit.
 */
static void ata_delay400(void)
{
    inb(ATA_CTL);
    inb(ATA_CTL);
    inb(ATA_CTL);
    inb(ATA_CTL);
}

/*
 * ata_poll — wait for the drive to become ready for a data transfer.
 *
 * Sequence (identical for reads and writes):
 *   1. 400 ns delay so the drive can assert BSY.
 *   2. Spin until BSY clears (or timeout).
 *   3. Return -1 if the ERR bit is set.
 *   4. Spin until DRQ is set (sector buffer ready).
 *
 * Returns 0 on success, -1 on error or timeout.
 */
static int ata_poll(void)
{
    ata_delay400();

    /* Wait for BSY to clear (up to ~100 000 iterations ≈ several ms). */
    uint32_t timeout = 100000;
    while (inb(ATA_STATUS) & ATA_SR_BSY) {
        if (!--timeout)
            return -1;   /* drive not responding */
    }

    /* Check for an ATA error condition. */
    uint8_t status = inb(ATA_STATUS);
    if (status & ATA_SR_ERR)
        return -1;

    /* Wait for DRQ — the drive's sector buffer is ready. */
    timeout = 100000;
    while (!(inb(ATA_STATUS) & ATA_SR_DRQ)) {
        if (!--timeout)
            return -1;
    }

    return 0;
}

/*
 * ata_setup — load LBA28 address and sector count into the task-file registers.
 *
 * Drive register encoding:
 *   bit 7 = 1 (must be set)
 *   bit 6 = 1 (LBA mode)
 *   bit 5 = 1 (must be set)
 *   bit 4 = drive select (0 = master, 1 = slave)
 *   bits 3-0 = LBA bits 24-27
 */
static void ata_setup(uint8_t drive, uint32_t lba, uint8_t count)
{
    outb(ATA_DRIVE,   0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_COUNT,   count);
    outb(ATA_LBA_LO,  (uint8_t)(lba        & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >>16) & 0xFF));
}

/* ── Public API ──────────────────────────────────────────────────────────── */

/*
 * ata_init — software-reset the primary ATA channel.
 *
 * Setting the SRST bit in the Device Control register resets both master
 * and slave on the channel simultaneously.  Clearing it then allows the
 * drives to complete their internal reset sequence (BIOS/UEFI usually does
 * this, but doing it ourselves guarantees a clean state).
 */
void ata_init(void)
{
    outb(ATA_CTL, 0x04);    /* SRST = 1 — assert software reset    */
    ata_delay400();
    outb(ATA_CTL, 0x00);    /* SRST = 0 — release reset            */
    ata_delay400();

    /* Wait for master to finish its reset sequence (BSY → 0, DRDY → 1). */
    uint32_t timeout = 100000;
    while ((inb(ATA_STATUS) & (ATA_SR_BSY | ATA_SR_DRDY)) != ATA_SR_DRDY) {
        if (!--timeout)
            break;   /* no disk — silent; reads/writes will fail gracefully */
    }
}

/*
 * ata_read — read `count` 512-byte sectors from `drive` starting at `lba`.
 *
 * Each sector requires one ata_poll() call (BSY→0, DRQ→1) followed by
 * reading 256 16-bit words from the data register.
 */
int ata_read(uint8_t drive, uint32_t lba, uint8_t count, void *buf)
{
    if (!count) return 0;

    uint16_t *dst = (uint16_t *)buf;

    ata_setup(drive, lba, count);
    outb(ATA_CMD, ATA_CMD_READ);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_poll() < 0)
            return -1;

        /* Read one sector (256 words = 512 bytes) from the data register. */
        for (int i = 0; i < 256; i++)
            dst[s * 256 + i] = inw(ATA_DATA);
    }

    return 0;
}

/*
 * ata_write — write `count` 512-byte sectors from `buf` to `drive` at `lba`.
 *
 * Each sector: ata_poll() (DRQ→1), then write 256 words.
 * After all sectors are written, a FLUSH WRITE CACHE command ensures the
 * drive's internal buffer is committed to persistent storage.
 */
int ata_write(uint8_t drive, uint32_t lba, uint8_t count, const void *buf)
{
    if (!count) return 0;

    const uint16_t *src = (const uint16_t *)buf;

    ata_setup(drive, lba, count);
    outb(ATA_CMD, ATA_CMD_WRITE);

    for (uint8_t s = 0; s < count; s++) {
        if (ata_poll() < 0)
            return -1;

        /* Write one sector (256 words = 512 bytes) to the data register. */
        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, src[s * 256 + i]);
    }

    /* Flush the drive's write cache. */
    outb(ATA_CMD, ATA_CMD_FLUSH);
    uint32_t timeout = 100000;
    while ((inb(ATA_STATUS) & ATA_SR_BSY) && --timeout);

    return timeout ? 0 : -1;
}
