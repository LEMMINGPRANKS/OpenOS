CC      = gcc
AS      = nasm
LD      = ld

CFLAGS  = -Wall -Wextra -std=c11 -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel -O2 -m64 \
          -fno-asynchronous-unwind-tables
ASFLAGS = -f elf64
LDFLAGS = -nostdlib -static --build-id=none -T linker.ld

OBJS = obj/boot.o obj/term.o obj/kmain.o

all: openos.iso

obj/boot.o: boot/boot.asm | obj
	$(AS) $(ASFLAGS) -o $@ $<

obj/%.o: kernel/%.c | obj
	$(CC) $(CFLAGS) -c -o $@ $<

obj:
	mkdir -p obj

kernel.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o kernel.bin $(OBJS)
	grub-file --is-x86-multiboot2 kernel.bin

iso_root/boot/grub/grub.cfg: grub/grub.cfg
	mkdir -p iso_root/boot/grub
	cp grub/grub.cfg iso_root/boot/grub/grub.cfg
	cp kernel.bin iso_root/boot/kernel.bin

openos.iso: kernel.bin iso_root/boot/grub/grub.cfg
	grub-mkrescue -o openos.iso iso_root

run: openos.iso
	qemu-system-x86_64 -cdrom openos.iso

headless: openos.iso
	qemu-system-x86_64 -cdrom openos.iso -display none -serial stdio -no-reboot

clean:
	rm -rf obj iso_root kernel.bin openos.iso

.PHONY: all run headless clean
