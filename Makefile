QEMU = qemu-system-riscv32
CC = /opt/homebrew/opt/llvm/bin/clang
CFLAGS = -std=c11 -O2 -g3 -Wall -Wextra --target=riscv32 -ffreestanding -nostdlib
SRCS = $(wildcard *.c)

run: kernel.elf
	$(QEMU) -machine virt -bios default -nographic -serial mon:stdio --no-reboot -kernel kernel.elf

kernel.elf: $(SRCS)
	$(CC) $(CFLAGS) -Wl,-Tkernel.ld -Wl,-Map=kernel.map -o kernel.elf $(SRCS)
