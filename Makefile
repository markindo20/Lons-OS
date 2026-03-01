CC      = x86_64-linux-gnu-gcc
LD      = x86_64-linux-gnu-ld

CFLAGS  = \
    -std=c11 \
    -ffreestanding \
    -fno-stack-protector \
    -fno-pic \
    -fno-pie \
    -mcmodel=kernel \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -m64 \
    -Wall -Wextra \
    -fno-unwind-tables \
    -fno-asynchronous-unwind-tables \
    -fno-exceptions \
    -Ikernel

LDFLAGS = \
    -T kernel/linker.ld \
    -nostdlib \
    -static \
    -z max-page-size=0x1000

SRCS = \
    kernel/kernel.c      \
    kernel/framebuffer.c \
    kernel/pmm.c         \
    kernel/gdt.c         \
    kernel/idt.c         \
    kernel/vmm.c         \
    kernel/heap.c        \
    kernel/pic.c         \
    kernel/pit.c         \
    kernel/rtc.c         \
    kernel/keyboard.c    \
    kernel/mouse.c       \
    kernel/sched.c       \
    kernel/gui.c         \
    kernel/vfs.c         \
    kernel/ramfs.c       \
    kernel/ata.c         \
    kernel/fat32.c

C_OBJS  = $(SRCS:.c=.o)
ASM_OBJ = kernel/context_switch.o
OBJS    = $(C_OBJS) $(ASM_OBJ)

ELF  = iso_root/boot/kernel.elf
ISO  = macos-lite.iso
DISK = disk.img

.PHONY: all clean run disk

all: $(ISO)

# Create a 64 MB FAT32 disk image (run once before first 'make run')
disk:
	dd if=/dev/zero of=$(DISK) bs=1M count=64
	mkfs.fat -F 32 -n "LONS_OS" $(DISK)
	@echo "disk.img created — run 'make run' to boot with it"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/context_switch.o: kernel/context_switch.S
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $(ELF)

$(ISO): $(ELF)
	cp limine/limine-bios.sys    iso_root/boot/
	cp limine/limine-bios-cd.bin iso_root/boot/
	xorriso -as mkisofs              \
	    -b boot/limine-bios-cd.bin   \
	    -no-emul-boot                \
	    -boot-load-size 4            \
	    -boot-info-table             \
	    --protective-msdos-label     \
	    iso_root -o $(ISO)
	./limine/limine bios-install $(ISO)

# Run with disk if it exists, without if it doesn't
run: $(ISO)
	@if [ -f $(DISK) ]; then \
	    qemu-system-x86_64 -cdrom $(ISO) -hda $(DISK) -m 256M; \
	else \
	    echo "No disk.img — run 'make disk' first for FAT32 support"; \
	    qemu-system-x86_64 -cdrom $(ISO) -m 256M; \
	fi

clean:
	rm -f $(C_OBJS) $(ASM_OBJ) $(ELF) $(ISO)

# Clean everything including the disk image
distclean: clean
	rm -f $(DISK)