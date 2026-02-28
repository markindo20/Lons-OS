# ──────────────────────────────────────────────
# Compiler & Linker
# ──────────────────────────────────────────────
CC      = x86_64-elf-gcc
LD      = x86_64-elf-ld
TARGET  = x86_64-elf   # We cross-compile for bare metal x86_64

# ──────────────────────────────────────────────
# Compiler Flags — explained below
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
    -fno-exceptions

# ──────────────────────────────────────────────
# Linker Flags
# ──────────────────────────────────────────────
LDFLAGS = \
    -T kernel/linker.ld \
    -nostdlib \
    -static

# ──────────────────────────────────────────────
# Source & Output
# ──────────────────────────────────────────────
SRC     = kernel/kernel.c
OBJ     = kernel/kernel.o
ELF     = iso_root/boot/kernel.elf
ISO     = macos-lite.iso

.PHONY: all clean run

all: $(ISO)

# Step 1: Compile C source → object file
$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

# Step 2: Link object → ELF binary
$(ELF): $(OBJ)
	$(LD) $(LDFLAGS) $(OBJ) -o $(ELF)

# Step 3: Build a bootable ISO using xorriso + Limine
$(ISO): $(ELF)
	cp limine/limine-bios.sys   iso_root/boot/
	cp limine/limine-bios-cd.bin iso_root/boot/
	xorriso -as mkisofs              \
	    -b boot/limine-bios-cd.bin   \
	    -no-emul-boot                \
	    -boot-load-size 4            \
	    -boot-info-table             \
	    --protective-msdos-label     \
	    iso_root -o $(ISO)
	./limine/limine bios-install $(ISO)

# Step 4: Run in QEMU
run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -m 256M

clean:
	rm -f $(OBJ) $(ELF) $(ISO)