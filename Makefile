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
    kernel/keyboard.c    \
    kernel/mouse.c       \
    kernel/gui.c

OBJS = $(SRCS:.c=.o)
ELF  = iso_root/boot/kernel.elf
ISO  = macos-lite.iso

.PHONY: all clean run

all: $(ISO)

%.o: %.c
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

run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -device ps2-mouse

clean:
	rm -f $(OBJS) $(ELF) $(ISO)