CC := gcc
CFLAGS := -m64 -ggdb -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -no-pie -mno-red-zone
AS := nasm
ASFLAGS :=
LD := ld
LDFLAGS :=
LDSCRIPT := kernel.ld

BUILDDIR := build
SRCDIR := src
USERDIR := user

KERNEL := $(BUILDDIR)/kernel.bin
DISK := $(BUILDDIR)/disk.img

C_SRCS := $(wildcard $(SRCDIR)/*.c)
C_OBJS := $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(C_SRCS))
S_SRCS := $(wildcard $(SRCDIR)/*.S)
S_OBJS := $(patsubst $(SRCDIR)/%.S, $(BUILDDIR)/%.o, $(S_SRCS))

USER_CFLAGS := -m64 -ffreestanding -fno-stack-protector -no-pie -mno-red-zone -nostdlib
USER_PROGS := $(BUILDDIR)/test.elf

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@$(CC) $(CFLAGS) -c -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.S | $(BUILDDIR)
	@$(CC) -c -o $@ $^

$(KERNEL): $(C_OBJS) $(S_OBJS) | $(BUILDDIR)
	@$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $^

$(BUILDDIR)/test.elf: $(USERDIR)/test.c $(USERDIR)/syscall.h $(USERDIR)/user.ld | $(BUILDDIR)
	@$(CC) $(USER_CFLAGS) -I$(USERDIR) -T $(USERDIR)/user.ld -o $@ $<

$(DISK): $(KERNEL) $(USER_PROGS) | limine
	@dd if=/dev/zero bs=1M count=64 of=$@
	@echo 'type=0c, bootable' | sfdisk -q $@
	@mformat -i $@@@1M -F ::
	@mmd -i $@@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	@mcopy -i $@@@1M $< ::/boot/kernel.sys
	@mcopy -i $@@@1M limine.conf limine/limine-bios.sys ::/boot/limine
	@mcopy -i $@@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	@mcopy -i $@@@1M $(BUILDDIR)/test.elf ::/test.elf
	@./limine/limine bios-install $@

limine:
	@make -C limine

run: $(DISK)
	qemu-system-x86_64 -machine q35 -serial stdio \
			-hda $<

disk: $(DISK)

clean:
	@rm -rf $(BUILDDIR)

$(BUILDDIR):
	@mkdir -p $@
