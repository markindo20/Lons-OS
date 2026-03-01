#pragma once
#include <stdint.h>

/*
 * ata.h — ATA PIO Driver (Primary bus, Master drive)
 *
 * Uses Programmed I/O (PIO) mode — the CPU reads/writes every byte
 * through I/O ports. Slower than DMA but simple and reliable for a
 * hobby OS. No interrupts used — we poll the status register.
 *
 * Primary ATA bus:
 *   I/O base  : 0x1F0
 *   Control   : 0x3F6
 *   IRQ       : 14 (we don't use it, polling only)
 *
 * LBA28 addressing: up to 128 GB disks.
 * Sector size: always 512 bytes.
 */

/* ── I/O Ports ── */
#define ATA_DATA        0x1F0   /* 16-bit data register             */
#define ATA_ERROR       0x1F1   /* Error register (read)            */
#define ATA_FEATURES    0x1F1   /* Features (write)                 */
#define ATA_SECTOR_CNT  0x1F2   /* Sector count                     */
#define ATA_LBA_LOW     0x1F3   /* LBA bits 0-7                     */
#define ATA_LBA_MID     0x1F4   /* LBA bits 8-15                    */
#define ATA_LBA_HIGH    0x1F5   /* LBA bits 16-23                   */
#define ATA_DRIVE_HEAD  0x1F6   /* Drive select + LBA bits 24-27    */
#define ATA_STATUS      0x1F7   /* Status register (read)           */
#define ATA_COMMAND     0x1F7   /* Command register (write)         */
#define ATA_CONTROL     0x3F6   /* Control / Alt-status             */

/* ── Status register bits ── */
#define ATA_SR_BSY   0x80   /* Controller is busy — wait           */
#define ATA_SR_DRDY  0x40   /* Drive ready for commands            */
#define ATA_SR_DRQ   0x08   /* Data transfer requested             */
#define ATA_SR_ERR   0x01   /* Error occurred                      */

/* ── ATA commands ── */
#define ATA_CMD_READ_SECTORS  0x20   /* Read  with retry              */
#define ATA_CMD_WRITE_SECTORS 0x30   /* Write with retry              */
#define ATA_CMD_IDENTIFY      0xEC   /* Identify drive                */
#define ATA_CMD_FLUSH         0xE7   /* Flush write cache to disk     */

#define ATA_SECTOR_SIZE 512

/*
 * ata_init — Detect and initialise the primary master ATA drive.
 * Returns 1 if a disk is present and ready, 0 otherwise.
 * Must be called after idt_init() (though we don't use IRQs).
 */
int ata_init(void);

/* Returns 1 if a disk was detected during ata_init(). */
int ata_is_present(void);

/* Total number of sectors on the disk (from IDENTIFY). */
uint32_t ata_sector_count(void);

/*
 * ata_read_sectors — Read `count` sectors starting at `lba` into `buf`.
 * buf must be at least count * 512 bytes.
 * Returns 0 on success, -1 on error/timeout.
 */
int ata_read_sectors(uint32_t lba, uint32_t count, void *buf);

/*
 * ata_write_sectors — Write `count` sectors from `buf` to disk at `lba`.
 * buf must be at least count * 512 bytes.
 * Returns 0 on success, -1 on error/timeout.
 */
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buf);