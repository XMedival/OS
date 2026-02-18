CC := gcc
CFLAGS := -m64 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -no-pie
AS := nasm
ASFLAGS :=
LD := ld
LDFLAGS :=
LDSCRIPT := kernel.ld

BUILDDIR := build
SRCDIR := src

KERNEL := $(BUILDDIR)/kernel.bin
DISK := $(BUILDDIR)/disk.img

C_SRCS := $(wildcard $(SRCDIR)/*.c)
C_OBJS := $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(C_SRCS))

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@$(CC) $(CFLAGS) -c -o $@ $^

$(KERNEL): $(C_OBJS) | $(BUILDDIR)
	@$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $^

$(DISK): $(KERNEL) | limine
	@dd if=/dev/zero bs=1M count=64 of=$@
	@echo 'type=0c, bootable' | sfdisk -q $@
	@mformat -i $@@@1M -F ::
	@mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	@mcopy -i $@@@1M $< ::/boot/kernel.sys
	@mcopy -i $@@@1M limine.conf limine/limine-bios.sys ::/boot/limine
	@mcopy -i $@@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	@./limine/limine bios-install $@

limine:
	@make -C limine

run: $(DISK)
	@qemu-system-x86_64 -machine q35 -serial stdio \
		-drive file=$<,if=none,id=disk0 \
		-device ahci,id=ahci \
		-device ide-hd,drive=disk0,bus=ahci.0

disk: $(DISK)

clean:
	@rm -rf $(BUILDDIR)

$(BUILDDIR):
	@mkdir -p $@
