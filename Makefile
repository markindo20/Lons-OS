# ──────────────────────────────────────────────
# Compiler & Linker
# ──────────────────────────────────────────────
CC      = x86_64-linux-gnu-gcc
LD      = x86_64-linux-gnu-ld

# ──────────────────────────────────────────────
# Compiler Flags
# ──────────────────────────────────────────────
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

# ──────────────────────────────────────────────
# Linker Flags
# ──────────────────────────────────────────────
LDFLAGS = \
    -T kernel/linker.ld \
    -nostdlib \
    -static \
    -z max-page-size=0x1000

# ──────────────────────────────────────────────
# Sources — add new .c files here as we grow
# ──────────────────────────────────────────────
SRCS = \
    kernel/kernel.c \
    kernel/pmm.c

# Auto-generate object file names from sources
OBJS = $(SRCS:.c=.o)

ELF  = iso_root/boot/kernel.elf
ISO  = macos-lite.iso

.PHONY: all clean run

all: $(ISO)

# Compile each .c → .o (pattern rule, works for all files)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link all objects → ELF
$(ELF): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $(ELF)

# Build bootable ISO
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

# Run in QEMU
run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M

clean:
	rm -f $(OBJS) $(ELF) $(ISO)