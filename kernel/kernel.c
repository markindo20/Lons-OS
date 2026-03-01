/*
 * kernel.c — Lons OS Entry Point – Step 12: ATA + FAT32
 */

#include "limine.h"
#include "framebuffer.h"
#include "pmm.h"
#include "gdt.h"
#include "idt.h"
#include "vmm.h"
#include "heap.h"
#include "pic.h"
#include "keyboard.h"
#include "mouse.h"
#include "gui.h"
#include "pit.h"
#include "rtc.h"
#include "sched.h"
#include "vfs.h"
#include "ata.h"
#include "fat32.h"

extern volatile uint64_t mouse_interrupt_count;

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request fb_req = { .id=LIMINE_FRAMEBUFFER_REQUEST, .revision=0 };
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_req = { .id=LIMINE_MEMMAP_REQUEST, .revision=0 };
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_req = { .id=LIMINE_HHDM_REQUEST, .revision=0 };
__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kaddr_req = { .id=LIMINE_KERNEL_ADDRESS_REQUEST, .revision=0 };
__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void cpu_halt(void) { for(;;) __asm__ volatile("cli; hlt"); }
#define kprint     fb_console_print
#define kprint_hex fb_console_print_hex
#define kprint_dec fb_console_print_dec

static void print_ok(void)              { fb_console_set_color(0x0044FF88,0); kprint("OK\n"); fb_console_set_color(0x00FFFFFF,0); }
static void print_warn(const char *m)   { fb_console_set_color(0x00FFAA00,0); kprint(m); kprint("\n"); fb_console_set_color(0x00FFFFFF,0); }
static void print_fail(const char *m)   { fb_console_set_color(0x00FF4444,0); kprint(m); kprint("\n"); fb_console_set_color(0x00FFFFFF,0); cpu_halt(); }

static int  k_strlen(const char *s) { int n=0; while(s[n]) n++; return n; }
static int  k_strcmp(const char *a, const char *b) { while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
static void k_strncpy(char *d, const char *s, int m) { int i=0; while(s[i]&&i<m-1){d[i]=s[i];i++;} d[i]=0; }
static void k_itoa(uint64_t v, char *buf) {
    if(!v){buf[0]='0';buf[1]=0;return;}
    char t[21]; int i=19; t[20]=0;
    while(v&&i>0){t[--i]=(char)('0'+(v%10));v/=10;}
    int j=0; while(t[i]) buf[j++]=t[i++]; buf[j]=0;
}

static int  term_wid  = -1;
static char input_line[256];
static int  input_len = 0;
static char cwd_display[VFS_PATH_MAX];

static void shell_prompt(void) {
    vfs_getcwd(cwd_display, VFS_PATH_MAX);
    gui_set_color(term_wid, 0x004488FF);
    gui_print(term_wid, "\n  "); gui_print(term_wid, cwd_display);
    gui_set_color(term_wid, 0x0044FF88);
    gui_print(term_wid, " $ ");
    gui_set_color(term_wid, GUI_WINFG);
}

static int ls_wid_g;
static void ls_cb(const char *name, int type, uint64_t size) {
    if (type==VFS_DIR) {
        gui_set_color(ls_wid_g, 0x004488FF);
        gui_print(ls_wid_g, "  [DIR]  "); gui_print(ls_wid_g, name); gui_print(ls_wid_g, "/\n");
    } else {
        gui_set_color(ls_wid_g, GUI_WINFG);
        gui_print(ls_wid_g, "  [FILE] "); gui_print(ls_wid_g, name);
        char sb[16]; k_itoa(size, sb);
        gui_print(ls_wid_g, "  ("); gui_print(ls_wid_g, sb); gui_print(ls_wid_g, "B)\n");
    }
    gui_set_color(ls_wid_g, GUI_WINFG);
}

#define MAX_ARGS 4
static char argv_buf[MAX_ARGS][128];
static int  argc_g;

static void parse_args(const char *line) {
    argc_g=0; const char *p=line;
    while(*p&&argc_g<MAX_ARGS) {
        while(*p==' ') p++;
        if(!*p) break;
        int i=0; while(*p&&*p!=' '&&i<127) argv_buf[argc_g][i++]=*p++;
        argv_buf[argc_g][i]=0; argc_g++;
    }
}

static void make_path(const char *arg, char *out, int max) {
    if (arg[0]=='/') { k_strncpy(out, arg, max); return; }
    vfs_getcwd(out, max);
    int cl=k_strlen(out);
    if(cl>0&&out[cl-1]!='/') { out[cl]='/'; out[++cl]=0; }
    k_strncpy(out+cl, arg, max-cl);
}

static void shell_run(const char *line) {
    if (!line[0]) return;
    parse_args(line);
    if (!argc_g) return;
    const char *cmd = argv_buf[0];

    if (k_strcmp(cmd,"help")==0) {
        gui_set_color(term_wid,0x004488FF);
        gui_print(term_wid,"\n  Commands\n  ─────────────────────────────\n");
        gui_set_color(term_wid,GUI_WINFG);
        gui_print(term_wid,"  ls [path]             list directory\n");
        gui_print(term_wid,"  cat <file>            print file\n");
        gui_print(term_wid,"  write <file> <text>   write to file\n");
        gui_print(term_wid,"  append <file> <text>  append to file\n");
        gui_print(term_wid,"  mkdir <dir>           make directory\n");
        gui_print(term_wid,"  rm <path>             delete\n");
        gui_print(term_wid,"  cd <dir>              change directory\n");
        gui_print(term_wid,"  pwd                   print cwd\n");
        gui_print(term_wid,"  stat <path>           file info\n");
        gui_print(term_wid,"  disk                  ATA disk info\n");
        gui_print(term_wid,"  mem / time / date / uptime / cls\n");
        return;
    }

    if (k_strcmp(cmd,"disk")==0) {
        gui_set_color(term_wid,0x004488FF);
        gui_print(term_wid,"\n  Disk Info\n  ─────────\n");
        gui_set_color(term_wid,GUI_WINFG);
        if (ata_is_present()) {
            char sb[16]; k_itoa((uint64_t)ata_sector_count()*512/1024/1024, sb);
            gui_print(term_wid,"  ATA disk: "); gui_print(term_wid, sb); gui_print(term_wid," MB\n");
            gui_print(term_wid,"  FAT32 mounted at /disk\n");
            gui_print(term_wid,"  Try: ls /disk   write /disk/hello.txt hi!\n");
        } else {
            gui_set_color(term_wid,0x00FFAA00);
            gui_print(term_wid,"  No disk detected. Run QEMU with -hda disk.img\n");
        }
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"ls")==0) {
        char path[VFS_PATH_MAX];
        if(argc_g>=2) make_path(argv_buf[1],path,VFS_PATH_MAX);
        else vfs_getcwd(path,VFS_PATH_MAX);
        ls_wid_g=term_wid;
        gui_set_color(term_wid,0x00AAAAAA);
        gui_print(term_wid,"\n  "); gui_print(term_wid,path); gui_print(term_wid,":\n");
        int n=vfs_readdir(path,ls_cb);
        if(n<0){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"  ");gui_print(term_wid,vfs_error_str(n));gui_print(term_wid,"\n");}
        else if(n==0){gui_set_color(term_wid,0x00AAAAAA);gui_print(term_wid,"  (empty)\n");}
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"cat")==0) {
        if(argc_g<2){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  Usage: cat <file>\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char path[VFS_PATH_MAX]; make_path(argv_buf[1],path,VFS_PATH_MAX);
        int fd=vfs_open(path,VFS_O_READ);
        if(fd<0){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(fd));gui_print(term_wid,"\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char buf[129]; int64_t n;
        gui_print(term_wid,"\n");
        while((n=vfs_read(fd,buf,128))>0) {
            buf[n]=0; char *p=buf; gui_print(term_wid,"  ");
            while(*p){char lb[128];int li=0;while(*p&&*p!='\n'&&li<126)lb[li++]=*p++;lb[li]=0;gui_print(term_wid,lb);if(*p=='\n'){gui_print(term_wid,"\n  ");p++;}}
        }
        gui_print(term_wid,"\n"); vfs_close(fd); return;
    }

    if (k_strcmp(cmd,"write")==0) {
        if(argc_g<3){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  Usage: write <file> <text>\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char path[VFS_PATH_MAX]; make_path(argv_buf[1],path,VFS_PATH_MAX);
        const char *c=line; int sk=0;
        while(c[sk]&&c[sk]!=' ')sk++; while(c[sk]==' ')sk++;
        while(c[sk]&&c[sk]!=' ')sk++; while(c[sk]==' ')sk++;
        c+=sk;
        int fd=vfs_open(path,VFS_O_WRITE|VFS_O_CREATE|VFS_O_TRUNC);
        if(fd<0){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(fd));gui_print(term_wid,"\n");gui_set_color(term_wid,GUI_WINFG);return;}
        uint64_t l=(uint64_t)k_strlen(c); vfs_write(fd,c,l); vfs_write(fd,"\n",1); vfs_close(fd);
        gui_set_color(term_wid,0x0044FF88); gui_print(term_wid,"\n  Written to "); gui_print(term_wid,path); gui_print(term_wid,"\n");
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"append")==0) {
        if(argc_g<3){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  Usage: append <file> <text>\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char path[VFS_PATH_MAX]; make_path(argv_buf[1],path,VFS_PATH_MAX);
        const char *c=line; int sk=0;
        while(c[sk]&&c[sk]!=' ')sk++; while(c[sk]==' ')sk++;
        while(c[sk]&&c[sk]!=' ')sk++; while(c[sk]==' ')sk++;
        c+=sk;
        int fd=vfs_open(path,VFS_O_WRITE|VFS_O_CREATE|VFS_O_APPEND);
        if(fd<0){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(fd));gui_print(term_wid,"\n");gui_set_color(term_wid,GUI_WINFG);return;}
        uint64_t l=(uint64_t)k_strlen(c); vfs_write(fd,c,l); vfs_write(fd,"\n",1); vfs_close(fd);
        gui_set_color(term_wid,0x0044FF88); gui_print(term_wid,"\n  Appended to "); gui_print(term_wid,path); gui_print(term_wid,"\n");
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"mkdir")==0) {
        if(argc_g<2){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  Usage: mkdir <dir>\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char path[VFS_PATH_MAX]; make_path(argv_buf[1],path,VFS_PATH_MAX);
        int r=vfs_mkdir(path);
        if(r==VFS_OK){gui_set_color(term_wid,0x0044FF88);gui_print(term_wid,"\n  Created: ");gui_print(term_wid,path);gui_print(term_wid,"\n");}
        else{gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(r));gui_print(term_wid,"\n");}
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"rm")==0) {
        if(argc_g<2){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  Usage: rm <path>\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char path[VFS_PATH_MAX]; make_path(argv_buf[1],path,VFS_PATH_MAX);
        int r=vfs_unlink(path);
        if(r==VFS_OK){gui_set_color(term_wid,0x0044FF88);gui_print(term_wid,"\n  Deleted: ");gui_print(term_wid,path);gui_print(term_wid,"\n");}
        else{gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(r));gui_print(term_wid,"\n");}
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"cd")==0) {
        char path[VFS_PATH_MAX];
        if(argc_g>=2) make_path(argv_buf[1],path,VFS_PATH_MAX); else{path[0]='/';path[1]=0;}
        int r=vfs_chdir(path);
        if(r!=VFS_OK){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(r));gui_print(term_wid,"\n");gui_set_color(term_wid,GUI_WINFG);}
        return;
    }

    if (k_strcmp(cmd,"pwd")==0) {
        vfs_getcwd(cwd_display,VFS_PATH_MAX);
        gui_set_color(term_wid,0x004488FF); gui_print(term_wid,"\n  "); gui_print(term_wid,cwd_display); gui_print(term_wid,"\n");
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"stat")==0) {
        if(argc_g<2){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  Usage: stat <path>\n");gui_set_color(term_wid,GUI_WINFG);return;}
        char path[VFS_PATH_MAX]; make_path(argv_buf[1],path,VFS_PATH_MAX);
        int type; uint64_t size; int r=vfs_stat(path,&type,&size);
        if(r!=VFS_OK){gui_set_color(term_wid,0x00FF4444);gui_print(term_wid,"\n  ");gui_print(term_wid,vfs_error_str(r));gui_print(term_wid,"\n");}
        else{char sb[16];k_itoa(size,sb);gui_set_color(term_wid,0x004488FF);gui_print(term_wid,"\n  Path: ");gui_print(term_wid,path);gui_print(term_wid,"\n  Type: ");gui_print(term_wid,type==VFS_DIR?"directory":"file");gui_print(term_wid,"\n  Size: ");gui_print(term_wid,sb);gui_print(term_wid," bytes\n");}
        gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"mem")==0) {
        gui_set_color(term_wid,0x004488FF); gui_print(term_wid,"\n  Memory\n  ──────\n"); gui_set_color(term_wid,GUI_WINFG);
        gui_print(term_wid,"  RAM:  "); gui_print_dec(term_wid,(pmm_get_free_pages()*4096)/(1024*1024)); gui_print(term_wid," MB free\n");
        gui_print(term_wid,"  Heap: "); gui_print_dec(term_wid,heap_get_free()/1024); gui_print(term_wid," KB free\n");
        return;
    }

    if (k_strcmp(cmd,"cls")==0) {
        gui_clear_window(term_wid);
        gui_set_color(term_wid,0x0044FF88); gui_print(term_wid,"  Lons OS Terminal\n"); gui_set_color(term_wid,GUI_WINFG); return;
    }

    if (k_strcmp(cmd,"time")==0) {
        rtc_time_t t; rtc_read(&t); char buf[9]; rtc_format_time(&t,buf);
        gui_set_color(term_wid,0x004488FF); gui_print(term_wid,"\n  Time: "); gui_set_color(term_wid,GUI_WINFG); gui_print(term_wid,buf); gui_print(term_wid,"\n"); return;
    }
    if (k_strcmp(cmd,"date")==0) {
        rtc_time_t t; rtc_read(&t); char buf[11]; rtc_format_date(&t,buf);
        gui_set_color(term_wid,0x004488FF); gui_print(term_wid,"\n  Date: "); gui_set_color(term_wid,GUI_WINFG); gui_print(term_wid,buf); gui_print(term_wid,"\n"); return;
    }
    if (k_strcmp(cmd,"uptime")==0) {
        uint64_t s=pit_get_seconds(),m=s/60,h=m/60;
        gui_set_color(term_wid,0x004488FF); gui_print(term_wid,"\n  Uptime: "); gui_set_color(term_wid,GUI_WINFG);
        gui_print_dec(term_wid,h); gui_print(term_wid,"h "); gui_print_dec(term_wid,m%60); gui_print(term_wid,"m "); gui_print_dec(term_wid,s%60); gui_print(term_wid,"s\n"); return;
    }

    gui_set_color(term_wid,0x00FF4444); gui_print(term_wid,"\n  Unknown: "); gui_print(term_wid,cmd); gui_print(term_wid,"  (type 'help')\n"); gui_set_color(term_wid,GUI_WINFG);
}

static void shell_key(char c) {
    if (c=='\n') { input_line[input_len]=0; gui_newline(term_wid); shell_run(input_line); input_len=0; shell_prompt(); }
    else if (c=='\b') { if(input_len>0){input_len--;gui_backspace(term_wid);} }
    else if (input_len<254) { char s[2]={c,0}; gui_print(term_wid,s); input_line[input_len++]=c; }
}

static uint8_t last_mouse_buttons=0;
static void handle_mouse_click(void) {
    uint8_t jc=mouse_buttons&~last_mouse_buttons; last_mouse_buttons=mouse_buttons;
    if(!(jc&MOUSE_LEFT)) return;
    for(int i=0;i<GUI_MAX_WINDOWS;i++){gui_window_t *w=&g_windows[i];if(!w->visible)continue;int ix=(mouse_x>=w->x&&mouse_x<w->x+(int32_t)w->w);int it=(mouse_y>=w->y&&mouse_y<w->y+TITLEBAR_H);if(ix&&it){gui_window_focus(i);return;}}
}

static void update_menubar_clock(void) {
    rtc_time_t t; rtc_read(&t); char tb[9]; rtc_format_time(&t,tb);
    uint32_t cx=g_fb.width-(8*8)-16, cy=(MENUBAR_H-16)/2;
    fb_fill_rect(cx,0,8*8+8,MENUBAR_H-1,GUI_MENUBAR); fb_print_at(cx,cy,tb,GUI_MENUFG,GUI_MENUBAR);
}

void _start(void) {
    if(!fb_req.response||fb_req.response->framebuffer_count==0) cpu_halt();
    fb_init(fb_req.response); fb_console_init();

    for(uint32_t y=0;y<36;y++) for(uint32_t x=0;x<g_fb.width;x++) fb_put_pixel(x,y,0x001E1E2E);
    fb_print_at(8,10,"Lons OS",0x00FFFFFF,0x001E1E2E);
    fb_print_at(g_fb.width-120,10,"Booting...",0x00AAAAAA,0x001E1E2E);

    fb_console_set_color(0x0044FF88,0); kprint("\n\n  Lons OS Boot Log\n  ----------------\n\n"); fb_console_set_color(0x00FFFFFF,0);

    kprint("  [GDT]  Descriptor tables...            "); gdt_init();  print_ok();
    kprint("  [IDT]  Exception handlers...           "); idt_init();  print_ok();

    if(!memmap_req.response||!memmap_req.response->entry_count) print_fail("  [FATAL] No memory map!");
    if(!hhdm_req.response)  print_fail("  [FATAL] No HHDM!");
    if(!kaddr_req.response) print_fail("  [FATAL] No kernel address!");

    uint64_t hhdm=hhdm_req.response->offset, kphys=kaddr_req.response->physical_base, kvirt=kaddr_req.response->virtual_base;

    kprint("  [PMM]  Physical memory...              "); pmm_init(memmap_req.response,hhdm); print_ok();
    kprint("  [VMM]  Page tables...                  "); vmm_init(hhdm,memmap_req.response,kphys,kvirt); print_ok();
    kprint("  [HEAP] Heap allocator...               "); heap_init(kvirt+(512ULL*1024*1024),16ULL*1024*1024); print_ok();
    kprint("  [PIC]  Interrupt controller...         "); pic_init();  print_ok();
    kprint("  [PIT]  Timer (100 Hz)...               "); pit_init();  print_ok();
    kprint("  [KBD]  Keyboard driver...              "); kbd_init();  print_ok();

    pic_unmask_irq(0); pic_unmask_irq(1); pic_unmask_irq(12);

    kprint("  [MOUSE] Mouse driver...                "); mouse_init(); print_ok();
    kprint("  [RTC]  Real-time clock...              ");
    { rtc_time_t t; rtc_read(&t); char tb[9],db[11]; rtc_format_time(&t,tb); rtc_format_date(&t,db); kprint(db); kprint(" "); kprint(tb); kprint(" "); } print_ok();
    kprint("  [SCHED] Multitasking...                "); sched_init(); print_ok();
    kprint("  [VFS]  Mounting RAMFS at / ...         "); vfs_init(); print_ok();

    kprint("  [ATA]  Detecting disk...               ");
    if (ata_init()) {
        print_ok();
        kprint("  [FAT32] Mounting /disk ...             ");
        vfs_node_t *fat_root = fat32_mount();
        if (fat_root && vfs_mount("/disk", fat_root) == VFS_OK) {
            print_ok();
        } else {
            print_warn("no FAT32 (format disk with mkfs.fat -F32)");
        }
    } else {
        print_warn("no disk (run QEMU with -hda disk.img)");
    }

    kprint("  [CPU]  Enabling interrupts...          "); __asm__ volatile("sti"); print_ok();
    kprint("  [GUI]  Starting window manager...      ");
    for(volatile int i=0;i<10000000;i++);

    gui_init();
    uint32_t ww=(g_fb.width*3)/4, wh=(g_fb.height*3)/4;
    int32_t  wx=(int32_t)((g_fb.width-ww)/2), wy=(int32_t)((g_fb.height-wh)/2)+MENUBAR_H/2;
    term_wid=gui_window_create(wx,wy,ww,wh,"Terminal");
    gui_window_focus(term_wid); print_ok();

    gui_set_color(term_wid,0x0044FF88); gui_print(term_wid,"  Welcome to Lons OS\n");
    gui_set_color(term_wid,0x00AAAAAA); gui_print(term_wid,"  ─────────────────────────────────────\n");
    gui_set_color(term_wid,GUI_WINFG);
    if (ata_is_present()) {
        gui_print(term_wid,"  Disk ready! Try:\n");
        gui_print(term_wid,"    ls /disk          disk info\n");
        gui_print(term_wid,"    write /disk/hi.txt hello world\n");
        gui_print(term_wid,"    cat /disk/hi.txt\n");
    } else {
        gui_print(term_wid,"  No disk. Add to Makefile run target:\n");
        gui_print(term_wid,"    -hda disk.img\n");
        gui_print(term_wid,"  Then: make disk && make run\n");
    }
    gui_set_color(term_wid,0x00AAAAAA); gui_print(term_wid,"  ─────────────────────────────────────\n"); gui_set_color(term_wid,GUI_WINFG);

    shell_prompt(); mouse_draw_cursor();
    uint64_t last_clock_tick=0;

    while(1) {
        __asm__ volatile("hlt");
        uint64_t nt=pit_get_ticks();
        if(nt-last_clock_tick>=PIT_HZ) { last_clock_tick=nt; mouse_erase_cursor(); update_menubar_clock(); mouse_draw_cursor(); }
        if(mouse_poll()) { mouse_erase_cursor(); handle_mouse_click(); mouse_draw_cursor(); }
        while(kbd_haschar()) { mouse_erase_cursor(); shell_key(kbd_getchar()); mouse_draw_cursor(); }
    }
}