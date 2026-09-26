CC      = gcc
AS      = nasm
LD      = ld

CFLAGS  = -Wall -Wextra -std=c11 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel -O2 -m64 \
          -fno-asynchronous-unwind-tables -mgeneral-regs-only
ASFLAGS = -f elf64
LDFLAGS = -nostdlib -static --build-id=none -z noexecstack -T linker.ld

OBJS = obj/boot.o obj/isr.o obj/term.o obj/idt.o obj/timer.o \
       obj/kb.o obj/shell.o obj/initrd.o obj/dev.o obj/panic.o \
       obj/mm.o obj/heap.o obj/font.o obj/gfx.o obj/mouse.o \
       obj/wm.o obj/apps.o obj/ramfs.o obj/files.o obj/path.o obj/js.o obj/ext.o obj/browser.o \
       obj/filemgr.o obj/pci.o obj/e1000.o obj/net.o obj/tcp.o \
       obj/getspgk.o obj/http.o obj/news.o obj/desktop.o obj/kmain.o obj/ata.o

all: openos.iso

obj/boot.o: boot/boot.asm | obj
	$(AS) $(ASFLAGS) -o $@ $<

obj/isr.o: kernel/isr.asm | obj
	$(AS) $(ASFLAGS) -o $@ $<

obj/%.o: kernel/%.c | obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj:
	mkdir -p obj

kernel.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.bin $(OBJS)
	grub-file --is-x86-multiboot2 kernel.bin

iso_root/boot/grub/grub.cfg: grub/grub.cfg kernel.bin initrd.tar
	mkdir -p iso_root/boot/grub
	cp grub/grub.cfg iso_root/boot/grub/grub.cfg
	cp kernel.bin iso_root/boot/kernel.bin
	cp initrd.tar iso_root/boot/initrd.tar

INITRD_FILES := $(shell find initrd -type f)

initrd.tar: $(INITRD_FILES)
	tar -cf initrd.tar -C initrd .

openos.iso: kernel.bin iso_root/boot/grub/grub.cfg
	grub-mkrescue -o openos.iso iso_root

run: openos.iso
	qemu-system-x86_64 -cdrom openos.iso

# DR1: a blank 16 MiB drive the OS can write to (survives across boots)
store.img:
	dd if=/dev/zero of=store.img bs=1M count=16

runstore: openos.iso store.img
	qemu-system-x86_64 -cdrom openos.iso -hda store.img

headless: openos.iso
	qemu-system-x86_64 -cdrom openos.iso -display none -serial stdio -no-reboot

clean:
	rm -rf obj iso_root kernel.bin openos.iso

.PHONY: all run headless clean
