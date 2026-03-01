/*
 * vfs.c — Virtual File System Core
 * Supports mount points via vfs_node_t.mount pointer.
 */

#include "vfs.h"
#include "ramfs.h"
#include "heap.h"

static vfs_node_t *vfs_root = 0;
static vfs_fd_t    fd_table[VFS_MAX_FDS];
static vfs_node_t *cwd = 0;

/* ── String helpers ── */
static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b;
}
static int vfs_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static void vfs_strncpy(char *d, const char *s, int max) {
    int i=0; while(s[i]&&i<max-1){d[i]=s[i];i++;} d[i]=0;
}

/* ── Path resolution ── */
static const char *next_comp(const char *path, char *out, int max) {
    while (*path=='/') path++;
    if (!*path) return 0;
    int i=0;
    while (*path && *path!='/' && i<max-1) out[i++]=*path++;
    out[i]=0;
    return path;
}

vfs_node_t *vfs_resolve(const char *path) {
    if (!path || !*path) return cwd;
    vfs_node_t *cur = (*path=='/') ? vfs_root : cwd;

    /* When entering a directory, follow mount points */
    if (cur->mount) cur = cur->mount;

    char comp[VFS_NAME_MAX];
    const char *rest = path;

    while (1) {
        rest = next_comp(rest, comp, VFS_NAME_MAX);
        if (!comp[0]) break;

        if (vfs_strcmp(comp, ".") == 0)  { if (!rest) break; continue; }
        if (vfs_strcmp(comp, "..") == 0) {
            if (cur->parent && cur->parent != cur) cur = cur->parent;
            if (!rest) break; continue;
        }
        if (cur->type != VFS_DIR) return 0;
        vfs_node_t *next = cur->ops->lookup(cur, comp);
        if (!next) return 0;
        /* Follow mount point when entering a directory */
        if (next->mount) next = next->mount;
        cur = next;
        if (!rest || !*rest) break;
    }
    return cur;
}

static vfs_node_t *resolve_parent(const char *path, char *out_name) {
    if (!path) return 0;
    int len = vfs_strlen(path);
    int last_slash = -1;
    for (int i=len-1;i>=0;i--) { if(path[i]=='/'){last_slash=i;break;} }
    if (last_slash < 0) { vfs_strncpy(out_name, path, VFS_NAME_MAX); return cwd; }
    if (last_slash == 0) { vfs_strncpy(out_name, path+1, VFS_NAME_MAX); return vfs_root; }
    char pp[VFS_PATH_MAX]; vfs_strncpy(pp, path, last_slash+1); pp[last_slash]=0;
    vfs_strncpy(out_name, path+last_slash+1, VFS_NAME_MAX);
    return vfs_resolve(pp);
}

static int alloc_fd(void) {
    for (int i=3;i<VFS_MAX_FDS;i++) if(!fd_table[i].used) return i;
    return -1;
}

/* ── vfs_init ── */
void vfs_init(void) {
    for (int i=0;i<VFS_MAX_FDS;i++) { fd_table[i].used=0; fd_table[i].node=0; fd_table[i].offset=0; }
    vfs_root = ramfs_init();
    cwd = vfs_root;

    vfs_mkdir("/home");
    vfs_mkdir("/tmp");
    vfs_mkdir("/sys");
    vfs_mkdir("/disk");   /* ← mount point for FAT32 */

    int fd = vfs_open("/home/welcome.txt", VFS_O_WRITE|VFS_O_CREATE);
    if (fd>=3) {
        const char *m = "Welcome to Lons OS!\nType 'help' for commands.\n";
        uint64_t l=0; while(m[l]) l++;
        vfs_write(fd, m, l); vfs_close(fd);
    }
    fd = vfs_open("/sys/version", VFS_O_WRITE|VFS_O_CREATE);
    if (fd>=3) {
        const char *v = "Lons OS v0.1 - Step 12 (ATA + FAT32)\n";
        uint64_t l=0; while(v[l]) l++;
        vfs_write(fd, v, l); vfs_close(fd);
    }
}

/* ── vfs_mount ── */
int vfs_mount(const char *path, vfs_node_t *fs_root) {
    if (!fs_root) return VFS_ERR_INVAL;
    vfs_node_t *mp = vfs_resolve(path);
    if (!mp) return VFS_ERR_NOENT;
    if (mp->type != VFS_DIR) return VFS_ERR_NOTDIR;
    /* Point the mount field to the filesystem root */
    mp->mount = fs_root;
    /* Set the fs_root's parent to the mount point's parent so ".." works */
    fs_root->parent = mp->parent ? mp->parent : vfs_root;
    return VFS_OK;
}

/* ── vfs_open ── */
int vfs_open(const char *path, int flags) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) {
        if (!(flags & VFS_O_CREATE)) return VFS_ERR_NOENT;
        char name[VFS_NAME_MAX];
        vfs_node_t *parent = resolve_parent(path, name);
        if (!parent) return VFS_ERR_NOENT;
        if (parent->type != VFS_DIR) return VFS_ERR_NOTDIR;
        node = parent->ops->create(parent, name, VFS_FILE);
        if (!node) return VFS_ERR_NOSPACE;
        node->parent = parent;
    } else {
        if (node->type == VFS_DIR) return VFS_ERR_ISDIR;
    }
    int fd = alloc_fd();
    if (fd < 0) return VFS_ERR_NOSPACE;
    fd_table[fd].used=1; fd_table[fd].node=node; fd_table[fd].flags=flags;
    if (flags & VFS_O_TRUNC)       { fd_table[fd].offset=0; node->size=0; }
    else if (flags & VFS_O_APPEND) { fd_table[fd].offset=node->size; }
    else                            fd_table[fd].offset=0;
    return fd;
}

/* ── vfs_close ── */
int vfs_close(int fd) {
    if (fd<0||fd>=VFS_MAX_FDS||!fd_table[fd].used) return VFS_ERR_BADF;
    fd_table[fd].used=0; fd_table[fd].node=0; fd_table[fd].offset=0;
    return VFS_OK;
}

/* ── vfs_read ── */
int64_t vfs_read(int fd, void *buf, uint64_t len) {
    if (fd<0||fd>=VFS_MAX_FDS||!fd_table[fd].used) return VFS_ERR_BADF;
    vfs_fd_t *f=&fd_table[fd];
    int64_t n=f->node->ops->read(f->node, f->offset, buf, len);
    if (n>0) f->offset+=(uint64_t)n;
    return n;
}

/* ── vfs_write ── */
int64_t vfs_write(int fd, const void *buf, uint64_t len) {
    if (fd<0||fd>=VFS_MAX_FDS||!fd_table[fd].used) return VFS_ERR_BADF;
    vfs_fd_t *f=&fd_table[fd];
    int64_t n=f->node->ops->write(f->node, f->offset, buf, len);
    if (n>0) { f->offset+=(uint64_t)n; if(f->offset>f->node->size) f->node->size=f->offset; }
    return n;
}

/* ── vfs_seek ── */
int64_t vfs_seek(int fd, int64_t offset, int whence) {
    if (fd<0||fd>=VFS_MAX_FDS||!fd_table[fd].used) return VFS_ERR_BADF;
    vfs_fd_t *f=&fd_table[fd];
    int64_t no;
    if      (whence==0) no=offset;
    else if (whence==1) no=(int64_t)f->offset+offset;
    else if (whence==2) no=(int64_t)f->node->size+offset;
    else return VFS_ERR_INVAL;
    if (no<0) no=0;
    f->offset=(uint64_t)no;
    return no;
}

/* ── vfs_mkdir ── */
int vfs_mkdir(const char *path) {
    if (vfs_resolve(path)) return VFS_ERR_EXIST;
    char name[VFS_NAME_MAX];
    vfs_node_t *parent = resolve_parent(path, name);
    if (!parent) return VFS_ERR_NOENT;
    if (parent->type != VFS_DIR) return VFS_ERR_NOTDIR;
    vfs_node_t *node = parent->ops->create(parent, name, VFS_DIR);
    if (!node) return VFS_ERR_NOSPACE;
    node->parent = parent;
    return VFS_OK;
}

/* ── vfs_unlink ── */
int vfs_unlink(const char *path) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return VFS_ERR_NOENT;
    if (node==vfs_root) return VFS_ERR_INVAL;
    vfs_node_t *parent = node->parent;
    if (!parent) return VFS_ERR_INVAL;
    return parent->ops->unlink(parent, node);
}

/* ── vfs_stat ── */
int vfs_stat(const char *path, int *type, uint64_t *size) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return VFS_ERR_NOENT;
    if (type) *type=node->type;
    if (size) *size=node->size;
    return VFS_OK;
}

/* ── vfs_readdir ── */
int vfs_readdir(const char *path, vfs_readdir_cb cb) {
    vfs_node_t *dir = vfs_resolve(path);
    if (!dir) return VFS_ERR_NOENT;
    if (dir->type!=VFS_DIR) return VFS_ERR_NOTDIR;
    uint32_t count = dir->ops->childcount(dir);
    for (uint32_t i=0;i<count;i++) {
        vfs_node_t *child = dir->ops->readdir(dir, i);
        if (child && cb) cb(child->name, child->type, child->size);
    }
    return (int)count;
}

/* ── vfs_chdir ── */
int vfs_chdir(const char *path) {
    vfs_node_t *node = vfs_resolve(path);
    if (!node) return VFS_ERR_NOENT;
    if (node->type!=VFS_DIR) return VFS_ERR_NOTDIR;
    cwd=node; return VFS_OK;
}

/* ── vfs_getcwd ── */
void vfs_getcwd(char *buf, int max) {
    if (!buf||max<=0) return;
    if (cwd==vfs_root) { buf[0]='/'; buf[1]=0; return; }
    char parts[16][VFS_NAME_MAX]; int depth=0;
    vfs_node_t *cur=cwd;
    while(cur && cur!=vfs_root && depth<16) {
        int i=0; while(cur->name[i]&&i<VFS_NAME_MAX-1){parts[depth][i]=cur->name[i];i++;}
        parts[depth][i]=0; depth++; cur=cur->parent;
    }
    int pos=0;
    for(int d=depth-1;d>=0&&pos<max-2;d--) {
        buf[pos++]='/';
        int i=0; while(parts[d][i]&&pos<max-1) buf[pos++]=parts[d][i++];
    }
    buf[pos]=0;
    if(pos==0){buf[0]='/';buf[1]=0;}
}

vfs_node_t *vfs_get_root(void) { return vfs_root; }

const char *vfs_error_str(int err) {
    switch(err) {
        case VFS_OK:          return "OK";
        case VFS_ERR_NOENT:   return "No such file or directory";
        case VFS_ERR_EXIST:   return "Already exists";
        case VFS_ERR_NOTDIR:  return "Not a directory";
        case VFS_ERR_ISDIR:   return "Is a directory";
        case VFS_ERR_NOSPACE: return "No space left";
        case VFS_ERR_BADF:    return "Bad file descriptor";
        case VFS_ERR_INVAL:   return "Invalid argument";
        case VFS_ERR_NOEMPTY: return "Directory not empty";
        default:              return "Unknown error";
    }
}