BOOT = minios.img
KERNEL = kernel_x86_64
USER = user_x86_64

# Replace CC and LD below with this version (for newer Macs only!)
CC = x86_64-linux-gnu-gcc
LD = x86_64-linux-gnu-ld

CFLAGS += -Wall -std=gnu11 -O1 -mno-red-zone -nostdinc -fno-stack-protector -pie -fno-zero-initialized-in-bss -c
LDFLAGS = --oformat=binary -T ./kernel/kernel.lds -nostdlib -melf_x86_64 -pie
KERNEL_OBJS = kernel/kernel_entry.o # Do not reorder this one
KERNEL_OBJS += kernel/kernel.o kernel/kernel_asm.o kernel/string.o kernel/fb.o kernel/printf.o kernel/ascii_font.o kernel/kernel_code.o kernel/kernel_malloc.o kernel/kernel_extra.o
USER_OBJS = user/user_entry.o # Do not reoder this one
USER_OBJS += user/user.o

UEFI_BIOS = /usr/share/qemu/OVMF.fd

all: $(BOOT)

test: $(BOOT)
	qemu-system-x86_64 -bios $(UEFI_BIOS) -m 1024 -drive format=raw,file=$(BOOT)

$(BOOT): $(KERNEL) $(USER) boot.efi
	@mkdir ./uefi_fat_mnt
	@dd if=/dev/zero of=$(BOOT) bs=1M count=1
	@mkfs.vfat $(BOOT)
	@sudo mount -o loop $(BOOT) ./uefi_fat_mnt
	@sudo mkdir -p ./uefi_fat_mnt/EFI/BOOT
	@sudo cp boot.efi uefi_fat_mnt/EFI/BOOT/BOOTx64.EFI
	@sudo cp $(KERNEL) uefi_fat_mnt/EFI/BOOT/KERNEL
	@sudo cp $(USER) uefi_fat_mnt/EFI/BOOT/USER
	@sudo umount ./uefi_fat_mnt
	@rmdir ./uefi_fat_mnt

$(KERNEL): $(KERNEL_OBJS) kernel/page_load.o
	$(LD) $(LDFLAGS) $^ -o $@

$(USER): $(USER_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -I ./kernel/include -c -o $@ $<

kernel/%.o: kernel/%.S
	$(CC) $(CFLAGS) -I ./kernel/include -c -o $@ $<

user/%.o: user/%.c
	$(CC) $(CFLAGS) -I ./user/include -c -o $@ $<

clean:
	@rm -rf $(KERNEL) $(USER) $(KERNEL_OBJS) $(USER_OBJS) $(BOOT) uefi_fat_mnt
