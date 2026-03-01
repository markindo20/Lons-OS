/*
 * ramfs.c — RAM Filesystem Implementation
 *
 * Provides the vfs_ops_t function table for an in-memory filesystem.
 * All storage is on the kernel heap via kmalloc/kfree.
 */

#include "ramfs.h"
#include "vfs.h"
#include "heap.h"

/* ─────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────── */

/* Our own memcpy/memset — no stdlib */
static void *r_memcpy(void *dst, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint64_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void r_memset(void *ptr, uint8_t val, uint64_t n) {
    uint8_t *p = (uint8_t *)ptr;
    for (uint64_t i = 0; i < n; i++) p[i] = val;
}

static int r_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void r_strncpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Allocate and zero a new vfs_node_t */
static vfs_node_t *alloc_node(const char *name, int type, vfs_ops_t *ops) {
    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!node) return 0;
    r_strncpy(node->name, name, VFS_NAME_MAX);
    node->type   = type;
    node->ops    = ops;
    node->size   = 0;
    node->parent = 0;
    return node;
}

/* Allocate a new ramfs_inode_t */
static ramfs_inode_t *alloc_inode(int type) {
    ramfs_inode_t *inode = (ramfs_inode_t *)kzalloc(sizeof(ramfs_inode_t));
    if (!inode) return 0;
    inode->type        = type;
    inode->data        = 0;
    inode->size        = 0;
    inode->capacity    = 0;
    inode->child_count = 0;
    return inode;
}

/* ─────────────────────────────────────────────
 * VFS ops implementation for RAMFS
 * ───────────────────────────────────────────── */

/* Forward declare ops table so create() can reference it */
static vfs_ops_t ramfs_ops;

/*
 * ramfs_lookup — Find a child node by name inside a directory.
 */
static vfs_node_t *ramfs_lookup(vfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_DIR) return 0;
    ramfs_inode_t *inode = (ramfs_inode_t *)dir->fs_data;
    for (int i = 0; i < inode->child_count; i++) {
        if (r_strcmp(inode->children[i]->name, name) == 0)
            return inode->children[i];
    }
    return 0;
}

/*
 * ramfs_create — Create a new file or directory inside dir.
 */
static vfs_node_t *ramfs_create(vfs_node_t *dir, const char *name, int type) {
    if (!dir || dir->type != VFS_DIR) return 0;
    ramfs_inode_t *parent_inode = (ramfs_inode_t *)dir->fs_data;

    if (parent_inode->child_count >= RAMFS_MAX_CHILDREN) return 0;

    /* Don't allow duplicate names */
    if (ramfs_lookup(dir, name)) return 0;

    /* Allocate new node + inode */
    vfs_node_t *node = alloc_node(name, type, &ramfs_ops);
    if (!node) return 0;

    ramfs_inode_t *inode = alloc_inode(type);
    if (!inode) { kfree(node); return 0; }

    /* Pre-allocate initial buffer for files */
    if (type == VFS_FILE) {
        inode->data = (uint8_t *)kmalloc(RAMFS_INITIAL_CAP);
        if (!inode->data) { kfree(inode); kfree(node); return 0; }
        inode->capacity = RAMFS_INITIAL_CAP;
        r_memset(inode->data, 0, RAMFS_INITIAL_CAP);
    }

    node->fs_data = inode;
    node->parent  = dir;

    /* Register in parent */
    parent_inode->children[parent_inode->child_count++] = node;

    return node;
}

/*
 * ramfs_read — Read bytes from a file node.
 */
static int64_t ramfs_read(vfs_node_t *node, uint64_t offset,
                           void *buf, uint64_t len) {
    if (!node || node->type != VFS_FILE) return VFS_ERR_ISDIR;
    ramfs_inode_t *inode = (ramfs_inode_t *)node->fs_data;

    if (offset >= inode->size) return 0;  /* EOF */

    uint64_t available = inode->size - offset;
    uint64_t to_read   = (len < available) ? len : available;

    r_memcpy(buf, inode->data + offset, to_read);
    return (int64_t)to_read;
}

/*
 * ramfs_write — Write bytes to a file node.
 * Grows the file's buffer if needed (up to RAMFS_MAX_FILESIZE).
 */
static int64_t ramfs_write(vfs_node_t *node, uint64_t offset,
                            const void *buf, uint64_t len) {
    if (!node || node->type != VFS_FILE) return VFS_ERR_ISDIR;
    ramfs_inode_t *inode = (ramfs_inode_t *)node->fs_data;

    uint64_t end = offset + len;
    if (end > RAMFS_MAX_FILESIZE) return VFS_ERR_NOSPACE;

    /* Grow buffer if needed */
    if (end > inode->capacity) {
        /* Double capacity until it fits */
        uint64_t new_cap = inode->capacity ? inode->capacity * 2 : RAMFS_INITIAL_CAP;
        while (new_cap < end) new_cap *= 2;
        if (new_cap > RAMFS_MAX_FILESIZE) new_cap = RAMFS_MAX_FILESIZE;

        uint8_t *new_data = (uint8_t *)kmalloc(new_cap);
        if (!new_data) return VFS_ERR_NOSPACE;

        r_memset(new_data, 0, new_cap);
        if (inode->data) {
            r_memcpy(new_data, inode->data, inode->size);
            kfree(inode->data);
        }
        inode->data     = new_data;
        inode->capacity = new_cap;
    }

    r_memcpy(inode->data + offset, buf, len);

    if (end > inode->size) {
        inode->size = end;
        node->size  = end;
    }

    return (int64_t)len;
}

/*
 * ramfs_unlink — Remove a node from its parent directory.
 * For files: frees the data buffer.
 * For directories: only allowed if empty.
 */
static int ramfs_unlink(vfs_node_t *parent, vfs_node_t *node) {
    if (!parent || !node) return VFS_ERR_INVAL;

    ramfs_inode_t *parent_inode = (ramfs_inode_t *)parent->fs_data;
    ramfs_inode_t *inode        = (ramfs_inode_t *)node->fs_data;

    /* Refuse to delete non-empty directories */
    if (node->type == VFS_DIR && inode->child_count > 0)
        return VFS_ERR_NOEMPTY;

    /* Find and remove from parent's children array */
    for (int i = 0; i < parent_inode->child_count; i++) {
        if (parent_inode->children[i] == node) {
            /* Shift remaining children down */
            for (int j = i; j < parent_inode->child_count - 1; j++)
                parent_inode->children[j] = parent_inode->children[j + 1];
            parent_inode->child_count--;
            break;
        }
    }

    /* Free the file's data buffer */
    if (node->type == VFS_FILE && inode->data) {
        kfree(inode->data);
    }

    kfree(inode);
    kfree(node);
    return VFS_OK;
}

/*
 * ramfs_readdir — Return the Nth child of a directory.
 */
static vfs_node_t *ramfs_readdir(vfs_node_t *dir, uint32_t index) {
    if (!dir || dir->type != VFS_DIR) return 0;
    ramfs_inode_t *inode = (ramfs_inode_t *)dir->fs_data;
    if ((int)index >= inode->child_count) return 0;
    return inode->children[index];
}

/*
 * ramfs_childcount — Return number of children in a directory.
 */
static uint32_t ramfs_childcount(vfs_node_t *dir) {
    if (!dir || dir->type != VFS_DIR) return 0;
    ramfs_inode_t *inode = (ramfs_inode_t *)dir->fs_data;
    return (uint32_t)inode->child_count;
}

/* ── The ops table ── */
static vfs_ops_t ramfs_ops = {
    .lookup     = ramfs_lookup,
    .create     = ramfs_create,
    .read       = ramfs_read,
    .write      = ramfs_write,
    .unlink     = ramfs_unlink,
    .readdir    = ramfs_readdir,
    .childcount = ramfs_childcount,
};

/* ─────────────────────────────────────────────
 * ramfs_init — Create and return the root node
 * ───────────────────────────────────────────── */
vfs_node_t *ramfs_init(void) {
    /* Create root inode */
    ramfs_inode_t *root_inode = alloc_inode(VFS_DIR);
    if (!root_inode) return 0;

    /* Create root node "/" */
    vfs_node_t *root = alloc_node("/", VFS_DIR, &ramfs_ops);
    if (!root) { kfree(root_inode); return 0; }

    root->fs_data = root_inode;
    root->parent  = root;  /* Root's parent is itself */

    return root;
}