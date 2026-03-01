/*
 * fat32.c — FAT32 Filesystem Driver
 *
 * Implements vfs_ops_t using an ATA disk as storage.
 * Supports: read, write, create, delete, mkdir, ls.
 * Supports LFN (Long File Name) read. Writes use 8.3 names.
 */

#include "fat32.h"
#include "ata.h"
#include "vfs.h"
#include "heap.h"

/* ─────────────────────────────────────────────
 * Internal state (one mounted FAT32 at a time)
 * ───────────────────────────────────────────── */
static struct {
    uint32_t fat_lba;          /* LBA of FAT table 1             */
    uint32_t data_lba;         /* LBA of first data cluster      */
    uint32_t root_cluster;     /* First cluster of root dir      */
    uint32_t sectors_per_clus; /* Sectors per cluster            */
    uint32_t bytes_per_clus;   /* Bytes per cluster              */
    uint32_t total_clusters;   /* Total data clusters            */
    int      mounted;
} g_fat32;

/* Private inode stored in vfs_node_t.fs_data */
typedef struct {
    uint32_t    first_cluster;     /* 0 = root dir (special)       */
    uint32_t    file_size;         /* bytes, files only            */
    int         is_dir;

    /* Where this entry lives on disk (needed to update size/cluster) */
    uint32_t    parent_cluster;    /* dir cluster holding our dirent */
    uint32_t    dirent_byte_off;   /* byte offset within that cluster */

    /* Cached children for directories */
    vfs_node_t *children[FAT32_MAX_DIR_CHILDREN];
    int         child_count;
    int         children_loaded;
} fat32_inode_t;

/* Single-sector scratch buffer (avoid stack allocation of 512 bytes) */
static uint8_t g_sect_buf[512];

/* ─────────────────────────────────────────────
 * String helpers (no stdlib)
 * ───────────────────────────────────────────── */
static int f_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int f_strcmp(const char *a, const char *b) {
    while (*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static void f_strncpy(char *d, const char *s, int max) {
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]=0;
}
static void f_memcpy(void *d, const void *s, uint32_t n) {
    uint8_t *dd=(uint8_t*)d; const uint8_t *ss=(const uint8_t*)s;
    for(uint32_t i=0;i<n;i++) dd[i]=ss[i];
}
static void f_memset(void *d, uint8_t v, uint32_t n) {
    uint8_t *dd=(uint8_t*)d; for(uint32_t i=0;i<n;i++) dd[i]=v;
}
static int f_toupper(int c) { return (c>='a'&&c<='z')?c-32:c; }

/* ─────────────────────────────────────────────
 * Cluster / sector helpers
 * ───────────────────────────────────────────── */

/* LBA of the first sector of a data cluster */
static uint32_t cluster_to_lba(uint32_t cluster) {
    return g_fat32.data_lba + (cluster - 2) * g_fat32.sectors_per_clus;
}

/* Read one sector into g_sect_buf */
static int read_sector(uint32_t lba) {
    return ata_read_sectors(lba, 1, g_sect_buf);
}

/* Write one sector from g_sect_buf */
static int write_sector(uint32_t lba) {
    return ata_write_sectors(lba, 1, g_sect_buf);
}

/* ─────────────────────────────────────────────
 * FAT table access
 * ───────────────────────────────────────────── */

/*
 * fat_get — Return the FAT32 entry for a cluster.
 * The FAT is an array of 32-bit values stored on disk.
 * Each entry occupies 4 bytes, so cluster N is at byte offset N*4
 * from the start of the FAT.
 */
static uint32_t fat_get(uint32_t cluster) {
    uint32_t fat_offset  = cluster * 4;
    uint32_t fat_sector  = g_fat32.fat_lba + fat_offset / 512;
    uint32_t fat_byte    = fat_offset % 512;

    if (read_sector(fat_sector) < 0) return 0x0FFFFFFF;

    uint32_t entry;
    f_memcpy(&entry, g_sect_buf + fat_byte, 4);
    return entry & 0x0FFFFFFF;  /* Mask reserved upper 4 bits */
}

/*
 * fat_set — Write a FAT32 entry for a cluster.
 * We read-modify-write to preserve the upper 4 reserved bits.
 */
static void fat_set(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_fat32.fat_lba + fat_offset / 512;
    uint32_t fat_byte   = fat_offset % 512;

    if (read_sector(fat_sector) < 0) return;

    uint32_t existing;
    f_memcpy(&existing, g_sect_buf + fat_byte, 4);
    uint32_t new_entry = (existing & 0xF0000000) | (value & 0x0FFFFFFF);
    f_memcpy(g_sect_buf + fat_byte, &new_entry, 4);
    write_sector(fat_sector);
}

/*
 * fat_alloc — Find a free cluster (value 0), mark it as end-of-chain,
 * return its number. Returns 0 if disk is full.
 */
static uint32_t fat_alloc(void) {
    for (uint32_t c = 2; c < g_fat32.total_clusters + 2; c++) {
        if (fat_get(c) == FAT32_FREE) {
            fat_set(c, 0x0FFFFFFF);  /* Mark as EOC */
            /* Zero out the cluster data */
            uint32_t lba = cluster_to_lba(c);
            f_memset(g_sect_buf, 0, 512);
            for (uint32_t s = 0; s < g_fat32.sectors_per_clus; s++)
                write_sector(lba + s);
            return c;
        }
    }
    return 0;  /* No free clusters */
}

/*
 * fat_free_chain — Follow and free every cluster in a chain.
 */
static void fat_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < 0x0FFFFFF7) {
        uint32_t next = fat_get(cluster);
        fat_set(cluster, FAT32_FREE);
        cluster = next;
    }
}

/* ─────────────────────────────────────────────
 * Name conversion helpers
 * ───────────────────────────────────────────── */

/*
 * parse_83_name — Convert a FAT 8.3 directory entry name to a
 * null-terminated C string.
 * "FOO     TXT" → "foo.txt"
 * "FOO        " → "foo"
 */
static void parse_83_name(const fat32_dirent_t *de, char *out) {
    int i = 0;

    /* Copy name (up to 8 chars, strip trailing spaces) */
    int name_len = 8;
    while (name_len > 0 && de->name[name_len - 1] == ' ') name_len--;

    for (int j = 0; j < name_len; j++) {
        char c = (char)de->name[j];
        /* Lowercase */
        if (c >= 'A' && c <= 'Z') c += 32;
        out[i++] = c;
    }

    /* Append extension if present */
    int ext_len = 3;
    while (ext_len > 0 && de->ext[ext_len - 1] == ' ') ext_len--;

    if (ext_len > 0) {
        out[i++] = '.';
        for (int j = 0; j < ext_len; j++) {
            char c = (char)de->ext[j];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[i++] = c;
        }
    }
    out[i] = 0;
}

/*
 * make_83_name — Convert "foo.txt" → name[8]="FOO     " ext[3]="TXT"
 */
static void make_83_name(const char *fname, uint8_t name_out[8], uint8_t ext_out[3]) {
    f_memset(name_out, ' ', 8);
    f_memset(ext_out,  ' ', 3);

    /* Find dot */
    int dot = -1;
    int len = f_strlen(fname);
    for (int i = len - 1; i >= 0; i--) {
        if (fname[i] == '.') { dot = i; break; }
    }

    int ni = 0;
    for (int i = 0; i < len && ni < 8; i++) {
        if (i == dot) break;
        name_out[ni++] = (uint8_t)f_toupper(fname[i]);
    }

    if (dot >= 0) {
        int ei = 0;
        for (int i = dot + 1; i < len && ei < 3; i++)
            ext_out[ei++] = (uint8_t)f_toupper(fname[i]);
    }
}

/*
 * lfn_to_ascii — Extract one LFN entry's characters (13 UTF-16 chars)
 * into a portion of a C string. UTF-16 → ASCII: just take low byte.
 */
static void lfn_to_ascii(const fat32_lfn_t *lfn, char *out) {
    /* The (order & 0x3F) - 1 gives 0-based index of this entry */
    /* Each LFN entry holds chars [(idx*13)..(idx*13+12)] */
    int idx = ((lfn->order & 0x3F) - 1) * 13;

    for (int i = 0; i < 5; i++) {
        uint16_t c = lfn->name1[i];
        if (c == 0 || c == 0xFFFF) { out[idx + i] = 0; return; }
        out[idx + i] = (char)(c & 0xFF);
    }
    for (int i = 0; i < 6; i++) {
        uint16_t c = lfn->name2[i];
        if (c == 0 || c == 0xFFFF) { out[idx + 5 + i] = 0; return; }
        out[idx + 5 + i] = (char)(c & 0xFF);
    }
    for (int i = 0; i < 2; i++) {
        uint16_t c = lfn->name3[i];
        if (c == 0 || c == 0xFFFF) { out[idx + 11 + i] = 0; return; }
        out[idx + 11 + i] = (char)(c & 0xFF);
    }
}

/* ─────────────────────────────────────────────
 * Directory scanning
 *
 * Iterate over all 32-byte entries in a cluster chain,
 * collecting file/dir names.
 * ───────────────────────────────────────────── */

/* Forward declaration */
static vfs_ops_t fat32_ops;

static vfs_node_t *make_node(const char *name, int type,
                              uint32_t first_cluster, uint32_t size,
                              uint32_t parent_cluster, uint32_t dirent_off) {
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) return 0;
    f_strncpy(node->name, name, VFS_NAME_MAX);
    node->type    = type;
    node->size    = size;
    node->ops     = &fat32_ops;
    node->mount   = 0;

    fat32_inode_t *inode = (fat32_inode_t *)kzalloc(sizeof(fat32_inode_t));
    if (!inode) { kfree(node); return 0; }
    inode->first_cluster    = first_cluster;
    inode->file_size        = size;
    inode->is_dir           = (type == VFS_DIR);
    inode->parent_cluster   = parent_cluster;
    inode->dirent_byte_off  = dirent_off;
    inode->child_count      = 0;
    inode->children_loaded  = 0;

    node->fs_data = inode;
    return node;
}

/*
 * load_dir_children — Scan a directory cluster chain and populate
 * the inode's children[] array. Called on first ls/lookup.
 */
static void load_dir_children(vfs_node_t *dir_node) {
    fat32_inode_t *dir_inode = (fat32_inode_t *)dir_node->fs_data;
    if (dir_inode->children_loaded) return;
    dir_inode->children_loaded = 1;

    uint32_t cluster = dir_inode->first_cluster;
    /* Root dir uses root_cluster */
    if (cluster == 0) cluster = g_fat32.root_cluster;

    char lfn_buf[256];
    int  lfn_pending = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF7) {
        uint32_t lba = cluster_to_lba(cluster);

        for (uint32_t s = 0; s < g_fat32.sectors_per_clus; s++) {
            if (read_sector(lba + s) < 0) goto next_cluster;

            /* Each sector holds 16 directory entries (512 / 32 = 16) */
            for (int e = 0; e < 16; e++) {
                uint32_t byte_off = (uint32_t)e * 32;
                fat32_dirent_t *de = (fat32_dirent_t *)(g_sect_buf + byte_off);

                /* End of directory */
                if (de->name[0] == 0x00) goto done;

                /* Deleted entry */
                if (de->name[0] == 0xE5) { lfn_pending = 0; continue; }

                /* LFN entry */
                if (de->attr == FAT32_ATTR_LFN) {
                    fat32_lfn_t *lfn = (fat32_lfn_t *)de;
                    if (lfn->order & 0x40) {
                        /* First (last in file) LFN entry — start fresh */
                        f_memset(lfn_buf, 0, 256);
                        lfn_pending = 1;
                    }
                    if (lfn_pending) lfn_to_ascii(lfn, lfn_buf);
                    continue;
                }

                /* Volume label — skip */
                if (de->attr & FAT32_ATTR_VOLID) { lfn_pending = 0; continue; }

                /* Skip . and .. */
                if (de->name[0] == '.') { lfn_pending = 0; continue; }

                /* Get name */
                char name[256];
                if (lfn_pending && lfn_buf[0]) {
                    f_strncpy(name, lfn_buf, 256);
                } else {
                    parse_83_name(de, name);
                }
                lfn_pending = 0;

                uint32_t first = ((uint32_t)de->first_cluster_high << 16)
                               | de->first_cluster_low;
                uint32_t fsize = de->file_size;
                int is_dir     = (de->attr & FAT32_ATTR_DIR) ? 1 : 0;

                /* Compute byte offset of this dirent in its cluster for later updates */
                uint32_t dirent_byte_in_cluster = s * 512 + byte_off;
                uint32_t dirent_abs_off = cluster * g_fat32.bytes_per_clus
                                        + dirent_byte_in_cluster;

                if (dir_inode->child_count < FAT32_MAX_DIR_CHILDREN) {
                    vfs_node_t *child = make_node(name,
                        is_dir ? VFS_DIR : VFS_FILE,
                        first, fsize,
                        cluster, dirent_abs_off);
                    if (child) {
                        child->parent = dir_node;
                        dir_inode->children[dir_inode->child_count++] = child;
                    }
                }
            }
        }
    next_cluster:
        cluster = fat_get(cluster);
    }
done:;
}

/* ─────────────────────────────────────────────
 * vfs_ops implementation for FAT32
 * ───────────────────────────────────────────── */

static vfs_node_t *fat32_lookup(vfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_DIR) return 0;
    fat32_inode_t *di = (fat32_inode_t *)dir->fs_data;
    load_dir_children(dir);
    for (int i = 0; i < di->child_count; i++) {
        if (f_strcmp(di->children[i]->name, name) == 0)
            return di->children[i];
    }
    return 0;
}

static vfs_node_t *fat32_readdir_fn(vfs_node_t *dir, uint32_t index) {
    if (!dir || dir->type != VFS_DIR) return 0;
    fat32_inode_t *di = (fat32_inode_t *)dir->fs_data;
    load_dir_children(dir);
    if ((int)index >= di->child_count) return 0;
    return di->children[index];
}

static uint32_t fat32_childcount(vfs_node_t *dir) {
    if (!dir || dir->type != VFS_DIR) return 0;
    fat32_inode_t *di = (fat32_inode_t *)dir->fs_data;
    load_dir_children(dir);
    return (uint32_t)di->child_count;
}

/*
 * fat32_read — Read len bytes from file at offset into buf.
 * Traverses the cluster chain, reading sector by sector.
 */
static int64_t fat32_read(vfs_node_t *node, uint64_t offset,
                           void *buf, uint64_t len) {
    if (!node || node->type != VFS_FILE) return VFS_ERR_ISDIR;
    fat32_inode_t *inode = (fat32_inode_t *)node->fs_data;

    if (offset >= inode->file_size) return 0;

    uint64_t available = inode->file_size - offset;
    uint64_t to_read   = (len < available) ? len : available;
    uint64_t remaining = to_read;

    uint8_t *out = (uint8_t *)buf;

    /* Walk cluster chain to the cluster containing `offset` */
    uint32_t cluster     = inode->first_cluster;
    uint64_t bytes_skipped = 0;

    while (cluster >= 2 && cluster < 0x0FFFFFF7 && remaining > 0) {
        /* Does this cluster contain the start offset? */
        if (bytes_skipped + g_fat32.bytes_per_clus > offset) {
            /* We're in the right cluster */
            uint32_t lba        = cluster_to_lba(cluster);
            uint32_t clus_off   = (uint32_t)(offset - bytes_skipped);

            for (uint32_t s = 0; s < g_fat32.sectors_per_clus && remaining > 0; s++) {
                uint32_t sect_start = s * 512;
                uint32_t sect_end   = sect_start + 512;

                if (clus_off >= sect_end) continue;  /* not at this sector yet */

                if (read_sector(lba + s) < 0) return VFS_ERR_INVAL;

                uint32_t read_from = (clus_off > sect_start) ? (clus_off - sect_start) : 0;
                uint32_t avail     = 512 - read_from;
                uint32_t copy_n    = (remaining < avail) ? (uint32_t)remaining : avail;

                f_memcpy(out, g_sect_buf + read_from, copy_n);
                out      += copy_n;
                offset   += copy_n;
                remaining -= copy_n;
                clus_off  += copy_n;
            }
        }
        bytes_skipped += g_fat32.bytes_per_clus;
        cluster = fat_get(cluster);
    }

    return (int64_t)(to_read - remaining);
}

/*
 * fat32_write — Write len bytes from buf into file at offset.
 * Allocates new clusters as needed, updates the directory entry size.
 */
static int64_t fat32_write(vfs_node_t *node, uint64_t offset,
                            const void *buf, uint64_t len) {
    if (!node || node->type != VFS_FILE) return VFS_ERR_ISDIR;
    if (!buf || len == 0) return 0;

    fat32_inode_t *inode = (fat32_inode_t *)node->fs_data;
    const uint8_t *src   = (const uint8_t *)buf;
    uint64_t written     = 0;

    /*
     * If the file has no cluster yet (new empty file), allocate one.
     */
    if (inode->first_cluster == 0) {
        uint32_t c = fat_alloc();
        if (!c) return VFS_ERR_NOSPACE;
        inode->first_cluster = c;

        /* Update the directory entry's first_cluster fields */
        /* We stored a combined byte offset; work backwards to lba + offset */
        uint32_t dc = inode->parent_cluster;
        uint32_t db = inode->dirent_byte_off % g_fat32.bytes_per_clus;
        uint32_t lba_de = cluster_to_lba(dc) + (db / 512);
        uint32_t off_de = db % 512;
        if (read_sector(lba_de) == 0) {
            fat32_dirent_t *de = (fat32_dirent_t *)(g_sect_buf + off_de);
            de->first_cluster_high = (uint16_t)(c >> 16);
            de->first_cluster_low  = (uint16_t)(c & 0xFFFF);
            write_sector(lba_de);
        }
    }

    /* Walk/extend cluster chain, writing data */
    uint64_t remaining = len;
    uint64_t file_offset = offset;

    /* Walk to the cluster containing file_offset, allocating if needed */
    uint32_t cluster   = inode->first_cluster;
    uint32_t prev_clus = 0;
    uint64_t clus_base = 0;  /* byte offset of start of current cluster */

    /* Skip to the right cluster */
    while (clus_base + g_fat32.bytes_per_clus <= file_offset) {
        uint32_t next = fat_get(cluster);
        if (next >= 0x0FFFFFF8) {
            /* Extend chain */
            uint32_t new_c = fat_alloc();
            if (!new_c) return written > 0 ? (int64_t)written : VFS_ERR_NOSPACE;
            fat_set(cluster, new_c);
            fat_set(new_c, 0x0FFFFFFF);
            next = new_c;
        }
        prev_clus  = cluster;
        cluster    = next;
        clus_base += g_fat32.bytes_per_clus;
    }
    (void)prev_clus;

    while (remaining > 0) {
        uint32_t lba      = cluster_to_lba(cluster);
        uint32_t clus_off = (uint32_t)(file_offset - clus_base);

        uint32_t sector_idx  = clus_off / 512;
        uint32_t sector_off  = clus_off % 512;

        while (sector_idx < g_fat32.sectors_per_clus && remaining > 0) {
            uint32_t copy_n = 512 - sector_off;
            if (copy_n > remaining) copy_n = (uint32_t)remaining;

            if (read_sector(lba + sector_idx) < 0) goto done;
            f_memcpy(g_sect_buf + sector_off, src, copy_n);
            write_sector(lba + sector_idx);

            src         += copy_n;
            written     += copy_n;
            file_offset += copy_n;
            remaining   -= copy_n;
            sector_off   = 0;
            sector_idx++;
        }

        if (remaining > 0) {
            /* Move to next cluster, allocate if needed */
            uint32_t next = fat_get(cluster);
            if (next >= 0x0FFFFFF8) {
                uint32_t new_c = fat_alloc();
                if (!new_c) break;
                fat_set(cluster, new_c);
                fat_set(new_c, 0x0FFFFFFF);
                next = new_c;
            }
            cluster   = next;
            clus_base += g_fat32.bytes_per_clus;
        }
    }

done:;
    /* Update file size if we grew the file */
    uint64_t new_end = offset + written;
    if (new_end > inode->file_size) {
        inode->file_size = (uint32_t)new_end;
        node->size       = (uint32_t)new_end;

        /* Update size in directory entry on disk */
        uint32_t dc = inode->parent_cluster;
        uint32_t db = inode->dirent_byte_off % g_fat32.bytes_per_clus;
        uint32_t lba_de = cluster_to_lba(dc) + (db / 512);
        uint32_t off_de = db % 512;
        if (read_sector(lba_de) == 0) {
            fat32_dirent_t *de = (fat32_dirent_t *)(g_sect_buf + off_de);
            de->file_size = (uint32_t)new_end;
            write_sector(lba_de);
        }
    }

    return (int64_t)written;
}

/*
 * fat32_create — Create a new file or directory inside a FAT32 directory.
 * Finds a free directory entry slot, initialises it, allocates a cluster
 * for directories.
 */
static vfs_node_t *fat32_create(vfs_node_t *dir, const char *name, int type) {
    if (!dir || dir->type != VFS_DIR) return 0;
    fat32_inode_t *dir_inode = (fat32_inode_t *)dir->fs_data;

    /* Duplicate check */
    if (fat32_lookup(dir, name)) return 0;

    uint8_t  n83[8], e83[3];
    make_83_name(name, n83, e83);

    /* Allocate a cluster for new directories; files start with cluster 0 */
    uint32_t new_cluster = 0;
    if (type == VFS_DIR) {
        new_cluster = fat_alloc();
        if (!new_cluster) return 0;
        /* Write . and .. entries */
        uint32_t lba = cluster_to_lba(new_cluster);
        f_memset(g_sect_buf, 0, 512);
        fat32_dirent_t *dot  = (fat32_dirent_t *)g_sect_buf;
        fat32_dirent_t *dot2 = (fat32_dirent_t *)(g_sect_buf + 32);

        f_memset(dot->name,  ' ', 8); dot->name[0]  = '.';
        f_memset(dot->ext,   ' ', 3); dot->attr = FAT32_ATTR_DIR;
        dot->first_cluster_high = (uint16_t)(new_cluster >> 16);
        dot->first_cluster_low  = (uint16_t)(new_cluster & 0xFFFF);

        f_memset(dot2->name, ' ', 8); dot2->name[0] = '.'; dot2->name[1] = '.';
        f_memset(dot2->ext,  ' ', 3); dot2->attr = FAT32_ATTR_DIR;
        uint32_t parent_c = dir_inode->first_cluster ? dir_inode->first_cluster : g_fat32.root_cluster;
        dot2->first_cluster_high = (uint16_t)(parent_c >> 16);
        dot2->first_cluster_low  = (uint16_t)(parent_c & 0xFFFF);

        write_sector(lba);
    }

    /* Find a free slot in the parent directory's cluster chain */
    uint32_t cluster = dir_inode->first_cluster;
    if (cluster == 0) cluster = g_fat32.root_cluster;
    uint32_t dirent_off = 0xFFFFFFFF;
    uint32_t dirent_cluster = 0;

    uint32_t scan_clus = cluster;
    int found = 0;

    while (scan_clus >= 2 && scan_clus < 0x0FFFFFF7 && !found) {
        uint32_t lba = cluster_to_lba(scan_clus);
        for (uint32_t s = 0; s < g_fat32.sectors_per_clus && !found; s++) {
            if (read_sector(lba + s) < 0) break;
            for (int e = 0; e < 16 && !found; e++) {
                fat32_dirent_t *de = (fat32_dirent_t *)(g_sect_buf + e * 32);
                if (de->name[0] == 0x00 || de->name[0] == 0xE5) {
                    /* Free slot — write entry here */
                    f_memset(de, 0, sizeof(fat32_dirent_t));
                    f_memcpy(de->name, n83, 8);
                    f_memcpy(de->ext,  e83, 3);
                    de->attr              = (type == VFS_DIR) ? FAT32_ATTR_DIR : FAT32_ATTR_ARCHIVE;
                    de->first_cluster_high= (uint16_t)(new_cluster >> 16);
                    de->first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
                    de->file_size         = 0;

                    /* Zero the next entry if it was 0x00 (mark new end) */
                    if (de->name[0] == 0x00 && e + 1 < 16) {
                        fat32_dirent_t *next_de = (fat32_dirent_t *)(g_sect_buf + (e+1)*32);
                        next_de->name[0] = 0x00;
                    }

                    write_sector(lba + s);

                    dirent_cluster = scan_clus;
                    dirent_off = scan_clus * g_fat32.bytes_per_clus + s * 512 + (uint32_t)(e * 32);
                    found = 1;
                }
            }
        }
        if (!found) {
            uint32_t next = fat_get(scan_clus);
            if (next >= 0x0FFFFFF8) {
                /* Extend directory cluster chain */
                uint32_t new_c = fat_alloc();
                if (!new_c) break;
                fat_set(scan_clus, new_c);
                fat_set(new_c, 0x0FFFFFFF);
                next = new_c;
            }
            scan_clus = next;
        }
    }

    if (!found) {
        if (type == VFS_DIR && new_cluster) fat_free_chain(new_cluster);
        return 0;
    }

    vfs_node_t *node = make_node(name, type, new_cluster, 0,
                                 dirent_cluster, dirent_off);
    if (!node) return 0;
    node->parent = dir;

    /* Register in parent's cache */
    if (dir_inode->child_count < FAT32_MAX_DIR_CHILDREN)
        dir_inode->children[dir_inode->child_count++] = node;

    return node;
}

/*
 * fat32_unlink — Mark a directory entry as deleted (0xE5),
 * free its cluster chain.
 */
static int fat32_unlink(vfs_node_t *parent, vfs_node_t *node) {
    if (!parent || !node) return VFS_ERR_INVAL;
    fat32_inode_t *inode  = (fat32_inode_t *)node->fs_data;
    fat32_inode_t *parent_inode = (fat32_inode_t *)parent->fs_data;

    if (node->type == VFS_DIR) {
        load_dir_children(node);
        if (inode->child_count > 0) return VFS_ERR_NOEMPTY;
    }

    /* Find and mark directory entry deleted */
    uint32_t dc = inode->parent_cluster;
    uint32_t db = inode->dirent_byte_off % g_fat32.bytes_per_clus;
    uint32_t lba_de = cluster_to_lba(dc) + (db / 512);
    uint32_t off_de = db % 512;
    if (read_sector(lba_de) == 0) {
        g_sect_buf[off_de] = 0xE5;
        write_sector(lba_de);
    }

    /* Free cluster chain */
    if (inode->first_cluster >= 2)
        fat_free_chain(inode->first_cluster);

    /* Remove from parent's child cache */
    for (int i = 0; i < parent_inode->child_count; i++) {
        if (parent_inode->children[i] == node) {
            for (int j = i; j < parent_inode->child_count - 1; j++)
                parent_inode->children[j] = parent_inode->children[j+1];
            parent_inode->child_count--;
            break;
        }
    }

    kfree(inode);
    kfree(node);
    return VFS_OK;
}

/* ── ops table ── */
static vfs_ops_t fat32_ops = {
    .lookup     = fat32_lookup,
    .create     = fat32_create,
    .read       = fat32_read,
    .write      = fat32_write,
    .unlink     = fat32_unlink,
    .readdir    = fat32_readdir_fn,
    .childcount = fat32_childcount,
};

/* ─────────────────────────────────────────────
 * fat32_mount — Parse BPB and return root vfs_node_t*
 * ───────────────────────────────────────────── */
vfs_node_t *fat32_mount(void) {
    if (!ata_is_present()) return 0;

    /* Read sector 0 (boot sector) */
    if (ata_read_sectors(0, 1, g_sect_buf) < 0) return 0;

    fat32_bpb_t *bpb = (fat32_bpb_t *)g_sect_buf;

    /* Validate: bytes_per_sector should be 512 */
    if (bpb->bytes_per_sector != 512) return 0;

    /* Validate FAT32 signature */
    if (bpb->fat_size_16 != 0 || bpb->root_entry_count != 0) return 0;  /* FAT12/16 */

    uint32_t fat_lba  = bpb->reserved_sectors;
    uint32_t data_lba = fat_lba + bpb->num_fats * bpb->fat_size_32;

    g_fat32.fat_lba          = fat_lba;
    g_fat32.data_lba         = data_lba;
    g_fat32.root_cluster     = bpb->root_cluster;
    g_fat32.sectors_per_clus = bpb->sectors_per_cluster;
    g_fat32.bytes_per_clus   = (uint32_t)bpb->sectors_per_cluster * 512;
    g_fat32.total_clusters   = (bpb->total_sectors_32 - data_lba) / bpb->sectors_per_cluster;
    g_fat32.mounted          = 1;

    /* Create root node */
    vfs_node_t *root = make_node("/", VFS_DIR, 0, 0, 0, 0);
    if (!root) return 0;
    root->parent = root;
    return root;
}