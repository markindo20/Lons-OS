#pragma once
#include <stdint.h>
#include <stddef.h>

#define VFS_FILE    1
#define VFS_DIR     2

#define VFS_O_READ   (1 << 0)
#define VFS_O_WRITE  (1 << 1)
#define VFS_O_CREATE (1 << 2)
#define VFS_O_TRUNC  (1 << 3)
#define VFS_O_APPEND (1 << 4)

#define VFS_MAX_FDS    32
#define VFS_PATH_MAX  256
#define VFS_NAME_MAX   64

#define VFS_OK           0
#define VFS_ERR_NOENT   -1
#define VFS_ERR_EXIST   -2
#define VFS_ERR_NOTDIR  -3
#define VFS_ERR_ISDIR   -4
#define VFS_ERR_NOSPACE -5
#define VFS_ERR_BADF    -6
#define VFS_ERR_INVAL   -7
#define VFS_ERR_NOEMPTY -8

typedef struct vfs_node vfs_node_t;

typedef struct {
    vfs_node_t *(*lookup)    (vfs_node_t *dir, const char *name);
    vfs_node_t *(*create)    (vfs_node_t *dir, const char *name, int type);
    int64_t     (*read)      (vfs_node_t *node, uint64_t offset, void *buf, uint64_t len);
    int64_t     (*write)     (vfs_node_t *node, uint64_t offset, const void *buf, uint64_t len);
    int         (*unlink)    (vfs_node_t *parent, vfs_node_t *node);
    vfs_node_t *(*readdir)   (vfs_node_t *dir, uint32_t index);
    uint32_t    (*childcount)(vfs_node_t *dir);
} vfs_ops_t;

struct vfs_node {
    char        name[VFS_NAME_MAX];
    int         type;
    uint64_t    size;
    vfs_ops_t  *ops;
    void       *fs_data;
    vfs_node_t *parent;
    vfs_node_t *mount;   /* if non-NULL: entering this dir redirects here */
};

typedef struct {
    int         used;
    vfs_node_t *node;
    uint64_t    offset;
    int         flags;
} vfs_fd_t;

/* Core API */
void        vfs_init(void);
int         vfs_open(const char *path, int flags);
int         vfs_close(int fd);
int64_t     vfs_read(int fd, void *buf, uint64_t len);
int64_t     vfs_write(int fd, const void *buf, uint64_t len);
int64_t     vfs_seek(int fd, int64_t offset, int whence);
int         vfs_mkdir(const char *path);
int         vfs_unlink(const char *path);
int         vfs_stat(const char *path, int *type, uint64_t *size);
int         vfs_chdir(const char *path);
void        vfs_getcwd(char *buf, int max);

/* Mount a filesystem root at path (creates the dir if needed) */
int         vfs_mount(const char *path, vfs_node_t *fs_root);

vfs_node_t *vfs_resolve(const char *path);
vfs_node_t *vfs_get_root(void);
const char *vfs_error_str(int err);

typedef void (*vfs_readdir_cb)(const char *name, int type, uint64_t size);
int vfs_readdir(const char *path, vfs_readdir_cb cb);