/*
 * ata.c — ATA PIO Driver
 *
 * Talks to the Primary Master ATA drive over I/O ports.
 * Uses LBA28 addressing (works for disks up to 128 GB).
 * Every transfer polls the status register — no interrupts.
 */

#include "ata.h"

/* ── I/O helpers ── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
static inline uint16_t inw(uint16_t port) {
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/* Short delay: read alt-status 4 times = ~400ns, lets drive settle */
static void ata_delay400(void) {
    inb(ATA_CONTROL); inb(ATA_CONTROL);
    inb(ATA_CONTROL); inb(ATA_CONTROL);
}

/* ── State ── */
static int      disk_present    = 0;
static uint32_t disk_sectors    = 0;

/* ─────────────────────────────────────────────
 * ata_wait_ready — Poll until BSY clears.
 * Returns 0 on success, -1 on timeout.
 * ───────────────────────────────────────────── */
static int ata_wait_ready(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR)  return -1;
        if (!(s & ATA_SR_BSY)) return 0;
    }
    return -1;  /* Timeout */
}

/* ─────────────────────────────────────────────
 * ata_wait_drq — Poll until DRQ (data ready) or error.
 * ───────────────────────────────────────────── */
static int ata_wait_drq(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_SR_ERR)  return -1;
        if (s & ATA_SR_DRQ)  return 0;
    }
    return -1;
}

/* ─────────────────────────────────────────────
 * ata_select_drive — Select master (drive 0) with LBA mode.
 * ───────────────────────────────────────────── */
static void ata_select_master(void) {
    /* 0xE0 = 1110 0000:
     *   Bit 7: always 1
     *   Bit 6: LBA mode (1 = LBA, 0 = CHS)
     *   Bit 5: always 1
     *   Bit 4: drive select (0 = master)
     *   Bits 3-0: LBA bits 24-27 (set later)
     */
    outb(ATA_DRIVE_HEAD, 0xE0);
    ata_delay400();
}

/* ─────────────────────────────────────────────
 * ata_init
 * ───────────────────────────────────────────── */
int ata_init(void) {
    disk_present = 0;
    disk_sectors = 0;

    /* Software reset: set SRST bit, then clear it */
    outb(ATA_CONTROL, 0x04);  /* SRST = 1 */
    outb(ATA_CONTROL, 0x00);  /* SRST = 0 */
    ata_delay400();

    if (ata_wait_ready() < 0) return 0;

    /* Select master drive in LBA mode */
    ata_select_master();
    if (ata_wait_ready() < 0) return 0;

    /* Check if any drive is present — if status is 0xFF the bus is floating */
    uint8_t status = inb(ATA_STATUS);
    if (status == 0xFF || status == 0x00) return 0;

    /* Send IDENTIFY command */
    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA_LOW,    0);
    outb(ATA_LBA_MID,    0);
    outb(ATA_LBA_HIGH,   0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay400();

    /* If status == 0 after IDENTIFY, no drive present */
    if (inb(ATA_STATUS) == 0) return 0;

    /* Wait for BSY to clear */
    if (ata_wait_ready() < 0) return 0;

    /* Check LBA_MID and LBA_HIGH — if non-zero it's not ATA (e.g. ATAPI CD-ROM) */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) return 0;

    /* Wait for DRQ */
    if (ata_wait_drq() < 0) return 0;

    /* Read IDENTIFY data (256 16-bit words) */
    uint16_t identify[256];
    for (int i = 0; i < 256; i++) identify[i] = inw(ATA_DATA);

    /*
     * Words 60-61: 28-bit LBA sector count
     * If word 83 bit 10 is set, LBA48 is supported (words 100-103),
     * but we'll use LBA28 for simplicity.
     */
    disk_sectors = ((uint32_t)identify[61] << 16) | identify[60];
    if (disk_sectors == 0) return 0;

    disk_present = 1;
    return 1;
}

int      ata_is_present(void)   { return disk_present; }
uint32_t ata_sector_count(void) { return disk_sectors; }

/* ─────────────────────────────────────────────
 * ata_setup_lba28 — Load LBA address and sector count into registers.
 * ───────────────────────────────────────────── */
static void ata_setup_lba28(uint32_t lba, uint8_t count) {
    outb(ATA_DRIVE_HEAD,  0xE0 | ((lba >> 24) & 0x0F));  /* LBA mode + bits 24-27 */
    outb(ATA_SECTOR_CNT,  count);
    outb(ATA_LBA_LOW,     (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,     (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_LBA_HIGH,    (uint8_t)((lba >> 16) & 0xFF));
}

/* ─────────────────────────────────────────────
 * ata_read_sectors
 * ───────────────────────────────────────────── */
int ata_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    if (!disk_present) return -1;
    if (count == 0)    return 0;

    uint16_t *ptr = (uint16_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        /* We send one sector at a time (count=1) — simpler */
        if (ata_wait_ready() < 0) return -1;

        ata_setup_lba28(lba + i, 1);
        outb(ATA_COMMAND, ATA_CMD_READ_SECTORS);
        ata_delay400();

        if (ata_wait_drq() < 0) return -1;

        /* Read 256 words (= 512 bytes) into buffer */
        for (int j = 0; j < 256; j++) {
            ptr[i * 256 + j] = inw(ATA_DATA);
        }

        ata_delay400();
    }
    return 0;
}

/* ─────────────────────────────────────────────
 * ata_write_sectors
 * ───────────────────────────────────────────── */
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    if (!disk_present) return -1;
    if (count == 0)    return 0;

    const uint16_t *ptr = (const uint16_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        if (ata_wait_ready() < 0) return -1;

        ata_setup_lba28(lba + i, 1);
        outb(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);
        ata_delay400();

        if (ata_wait_drq() < 0) return -1;

        /* Write 256 words */
        for (int j = 0; j < 256; j++) {
            outw(ATA_DATA, ptr[i * 256 + j]);
        }

        /* Flush write cache */
        outb(ATA_COMMAND, ATA_CMD_FLUSH);
        if (ata_wait_ready() < 0) return -1;

        ata_delay400();
    }
    return 0;
}