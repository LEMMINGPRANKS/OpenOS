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
       obj/getspgk.o obj/http.o obj/news.o obj/desktop.o obj/kmain.o obj/ata.o obj/tar.o obj/store.o

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

# --- OpenBIOS: our own bootloader, no GRUB ---------------------------------

kernel.flat: kernel.bin
	objcopy -O binary kernel.bin kernel.flat

bios/mbr.bin: bios/mbr.asm bios/layout.inc
	$(AS) -f bin -I bios/ -o $@ bios/mbr.asm

bios/stage2.bin: bios/stage2.asm bios/layout.inc kernel.flat
	$(AS) -f bin -I bios/ -o $@ bios/stage2.asm \
	    -D ENTRY_OFF=$(shell printf '%d' $$(( $$(nm kernel.bin | awk '/ T _start$$/ {print "0x"$$1}') - 0x100000 ))) \
	    -D KERNEL_SECTORS=$(shell echo $$(( ($$(stat -c%s kernel.flat) + 511) / 512 ))) \
	    -D BSS_END_OFF=$(shell printf '%d' $$(( $$(nm kernel.bin | awk '/ B __kernel_end$$/ {print "0x"$$1}') - 0x100000 )))

# the DR1 seed: every initrd/ file pre-packed as the on-disk store, so
# OpenOS boots with its whole filesystem on DR1 (no initramfs in the boot)
store-seed.img: tools/mkstore.py $(INITRD_FILES)
	python3 tools/mkstore.py initrd store-seed.img

openos.img: bios/mbr.bin bios/stage2.bin kernel.flat store-seed.img
	dd if=/dev/zero of=openos.img bs=1M count=16 status=none
	dd if=bios/mbr.bin     of=openos.img                    conv=notrunc status=none
	dd if=bios/stage2.bin  of=openos.img bs=512 seek=1      conv=notrunc status=none
	dd if=kernel.flat      of=openos.img bs=512 seek=128    conv=notrunc status=none
	dd if=store-seed.img   of=openos.img bs=512 seek=2048   conv=notrunc status=none
	test $$(stat -c%s kernel.flat) -le $$(( 512 * 1024 ))

imgrun: openos.img
	qemu-system-x86_64 -drive file=openos.img,format=raw,if=ide,index=0,media=disk

imgheadless: openos.img
	qemu-system-x86_64 -drive file=openos.img,format=raw,if=ide,index=0,media=disk \
	    -display none -no-reboot -serial stdio

# the real thing: boot OUR bootloader, no GRUB anywhere
run: openos.img
	qemu-system-x86_64 -drive file=openos.img,format=raw,if=ide,index=0,media=disk

# GRUB fallback for comparison
isorun: openos.iso
	qemu-system-x86_64 -cdrom openos.iso

# DR1: a blank 16 MiB drive the OS can write to (survives across boots)
store.img:
	dd if=/dev/zero of=store.img bs=1M count=16

runstore: openos.iso store.img
	qemu-system-x86_64 -cdrom openos.iso -hda store.img

headless: openos.iso
	qemu-system-x86_64 -cdrom openos.iso -display none -serial stdio -no-reboot

clean:
	rm -rf obj iso_root kernel.bin openos.iso openos.img kernel.flat bios/*.bin

.PHONY: all run headless clean imgrun imgheadless isorun
