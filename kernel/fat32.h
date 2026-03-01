#pragma once
#include <stdint.h>
#include "vfs.h"

/*
 * fat32.h — FAT32 Filesystem
 *
 * Disk layout:
 *
 *   [ Boot sector / BPB ]   LBA 0
 *   [ Reserved sectors  ]
 *   [ FAT table 1       ]   LBA = reserved_sectors
 *   [ FAT table 2       ]   (backup)
 *   [ Data clusters     ]   starts at cluster 2
 *       Cluster 2 = root directory
 *       Cluster 3+ = files and subdirectories
 *
 * Cluster addressing:
 *   data_lba(cluster) = data_start + (cluster - 2) * sectors_per_cluster
 *
 * FAT entry (32 bits, upper 4 bits reserved/ignored):
 *   0x00000000 = free cluster
 *   0x0FFFFFF8+ = end of chain
 *   anything else = next cluster in chain
 *
 * Directory entry = 32 bytes (8.3 format)
 * LFN entry       = 32 bytes (attribute 0x0F), precedes its 8.3 entry
 */

/* BPB — maps directly onto boot sector bytes 0-89 */
typedef struct {
    uint8_t  jump[3];               /* 0  */
    uint8_t  oem[8];                /* 3  */
    uint16_t bytes_per_sector;      /* 11 */
    uint8_t  sectors_per_cluster;   /* 13 */
    uint16_t reserved_sectors;      /* 14 */
    uint8_t  num_fats;              /* 16 */
    uint16_t root_entry_count;      /* 17 (0 for FAT32) */
    uint16_t total_sectors_16;      /* 19 (0 for FAT32) */
    uint8_t  media_type;            /* 21 */
    uint16_t fat_size_16;           /* 22 (0 for FAT32) */
    uint16_t sectors_per_track;     /* 24 */
    uint16_t num_heads;             /* 26 */
    uint32_t hidden_sectors;        /* 28 */
    uint32_t total_sectors_32;      /* 32 */
    /* FAT32 extended BPB */
    uint32_t fat_size_32;           /* 36 — sectors per FAT */
    uint16_t ext_flags;             /* 40 */
    uint16_t fs_version;            /* 42 */
    uint32_t root_cluster;          /* 44 — first cluster of root dir */
    uint16_t fs_info;               /* 48 */
    uint16_t backup_boot;           /* 50 */
    uint8_t  reserved[12];          /* 52 */
    uint8_t  drive_number;          /* 64 */
    uint8_t  reserved1;             /* 65 */
    uint8_t  boot_signature;        /* 66 */
    uint32_t volume_id;             /* 67 */
    uint8_t  volume_label[11];      /* 71 */
    uint8_t  fs_type[8];            /* 82 */
} __attribute__((packed)) fat32_bpb_t;

/* 8.3 directory entry */
typedef struct {
    uint8_t  name[8];               /* Space-padded, uppercase          */
    uint8_t  ext[3];                /* Extension, space-padded          */
    uint8_t  attr;                  /* File attributes                  */
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;    /* High 16 bits of first cluster    */
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_low;     /* Low 16 bits of first cluster     */
    uint32_t file_size;             /* File size in bytes               */
} __attribute__((packed)) fat32_dirent_t;

/* LFN (Long File Name) entry — attribute 0x0F */
typedef struct {
    uint8_t  order;         /* Sequence number (1-based, 0x40=last) */
    uint16_t name1[5];      /* UTF-16 chars 1-5                     */
    uint8_t  attr;          /* 0x0F                                 */
    uint8_t  type;          /* 0                                    */
    uint8_t  checksum;      /* Checksum of 8.3 name                 */
    uint16_t name2[6];      /* UTF-16 chars 6-11                    */
    uint16_t cluster;       /* 0                                    */
    uint16_t name3[2];      /* UTF-16 chars 12-13                   */
} __attribute__((packed)) fat32_lfn_t;

/* Directory entry attributes */
#define FAT32_ATTR_READONLY  0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLID     0x08
#define FAT32_ATTR_DIR       0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F   /* All 4 lower attr bits set = LFN */

/* FAT cluster values */
#define FAT32_FREE  0x00000000
#define FAT32_BAD   0x0FFFFFF7
#define FAT32_EOC   0x0FFFFFF8   /* End-of-chain threshold (>= = EOC) */

/* Max children we cache per directory */
#define FAT32_MAX_DIR_CHILDREN 128

/*
 * fat32_mount — Parse the FAT32 boot sector from the disk,
 * initialise internal state, and return a vfs_node_t* for
 * the root directory.
 * Returns NULL if the disk has no valid FAT32 filesystem.
 */
vfs_node_t *fat32_mount(void);