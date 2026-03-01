#pragma once
#include <stdint.h>
#include "vfs.h"

/*
 * ramfs.h — RAM Filesystem
 *
 * A simple in-memory filesystem backed by the kernel heap.
 * All files and directories are allocated with kmalloc().
 * Data survives as long as the machine is running.
 * Nothing is persisted to disk (yet).
 *
 * Limits:
 *   Max nodes total:    RAMFS_MAX_NODES    (files + dirs combined)
 *   Max children/dir:   RAMFS_MAX_CHILDREN
 *   Max file size:      RAMFS_MAX_FILESIZE
 *   Max filename:       VFS_NAME_MAX (from vfs.h)
 *
 * Internal node layout:
 *
 *   ramfs_inode_t        ← internal metadata (heap allocated)
 *     ├── type           FILE or DIR
 *     ├── data ptr       for files: heap buffer holding content
 *     ├── size           bytes written so far
 *     ├── capacity       bytes allocated in data buffer
 *     ├── children[]     for dirs: pointers to child vfs_node_t
 *     └── child_count    number of children
 *
 * The vfs_node_t.fs_data pointer points to the ramfs_inode_t.
 */

#define RAMFS_MAX_NODES     512
#define RAMFS_MAX_CHILDREN   64
#define RAMFS_MAX_FILESIZE  (512 * 1024)   /* 512 KB per file */
#define RAMFS_INITIAL_CAP      256         /* Initial alloc for new file */

typedef struct {
    int         type;           /* VFS_FILE or VFS_DIR              */

    /* File data */
    uint8_t    *data;           /* Heap buffer for file content     */
    uint64_t    size;           /* Bytes of valid content           */
    uint64_t    capacity;       /* Total allocated bytes            */

    /* Directory children */
    vfs_node_t *children[RAMFS_MAX_CHILDREN];
    int         child_count;
} ramfs_inode_t;

/*
 * ramfs_init — Create the root ramfs node.
 * Returns a vfs_node_t* for the root directory ("/").
 * Pass this to vfs_mount().
 */
vfs_node_t *ramfs_init(void);