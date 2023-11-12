QEMU = qemu-system-riscv32
CC = /opt/homebrew/opt/llvm/bin/clang
OBJCOPY = /opt/homebrew/opt/llvm/bin/llvm-objcopy
CFLAGS = -std=c11 -O2 -g3 -Wall -Wextra --target=riscv32 -ffreestanding -nostdlib
KERNEL_SRCS = common.c kernel.c
KERNEL_HDRS = common.h kernel.h
USER_SRCS = common.c shell.c user.c
USER_HDRS = common.h user.h

run: kernel.elf shell.elf
	$(QEMU) -machine virt -bios default -nographic -serial mon:stdio --no-reboot -kernel kernel.elf

kernel.elf: $(KERNEL_SRCS) $(KERNEL_HDRS)
	$(CC) $(CFLAGS) -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf $(KERNEL_SRCS) shell.bin.o

shell.elf: $(USER_SRCS) $(USER_HDRS)
	$(CC) $(CFLAGS) -Wl,-Tuser.ld -Wl,-Map=shell.map -o shell.elf $(USER_SRCS)
	$(OBJCOPY) --set-section-flags .bss=alloc,contents -O binary shell.elf shell.bin
	$(OBJCOPY) -Ibinary -Oelf32-littleriscv shell.bin shell.bin.o
