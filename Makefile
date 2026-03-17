NASM       = nasm
GCC        = gcc
LD         = ld

CFLAGS     = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -O2
USER_CFLAGS = -m32 -ffreestanding -fno-pie -static -nostdlib -nostartfiles \
              -Wl,--build-id=none -O2

BOOT_BIN   = build/boot.bin
KERNEL_BIN = build/kernel.bin
OS_IMG     = build/os.img
DISK_IMG   = build/disk.img

all: $(OS_IMG) $(DISK_IMG)

$(BOOT_BIN): boot/boot.asm | build
	$(NASM) -f bin -o $@ $<

build/entry32.o: kernel/entry32.asm | build
	$(NASM) -f elf32 -o $@ $<

build/kernel32.o: kernel/kernel32.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/vga.o: kernel/vga.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/idt.o: kernel/idt.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/isr.o: kernel/isr.asm | build
	$(NASM) -f elf32 -o $@ $<

build/irq.o: kernel/irq.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/timer.o: kernel/timer.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/keyboard.o: kernel/keyboard.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/pmm.o: kernel/pmm.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/string.o: kernel/string.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/paging.o: kernel/paging.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/heap.o: kernel/heap.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/fault.o: kernel/fault.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/gdt.o: kernel/gdt.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/syscall.o: kernel/syscall.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/task.o: kernel/task.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/task_switch.o: kernel/task_switch.asm | build
	$(NASM) -f elf32 -o $@ $<

build/sched.o: kernel/sched.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/uvm.o: kernel/uvm.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/user_enter.o: kernel/user_enter.asm | build
	$(NASM) -f elf32 -o $@ $<

build/ata.o: kernel/ata.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/alfs.o: kernel/alfs.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/elf.o: kernel/elf.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/shell.o: kernel/shell.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/serial.o: kernel/serial.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/pipe.o: kernel/pipe.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

build/nano.o: kernel/nano.c | build
	$(GCC) $(CFLAGS) -c -o $@ $<

KERNEL_OBJS = build/entry32.o build/kernel32.o build/vga.o \
              build/gdt.o build/idt.o build/isr.o build/irq.o build/timer.o \
              build/keyboard.o build/pmm.o build/string.o \
              build/paging.o build/heap.o build/fault.o build/syscall.o \
              build/task.o build/task_switch.o build/sched.o \
              build/uvm.o build/user_enter.o build/ata.o build/alfs.o build/elf.o \
              build/shell.o build/serial.o build/pipe.o build/nano.o

build/kernel.elf: $(KERNEL_OBJS) kernel/kernel.ld | build
	$(LD) -m elf_i386 -T kernel/kernel.ld -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): build/kernel.elf | build
	objcopy -O binary $< $@

$(OS_IMG): $(BOOT_BIN) $(KERNEL_BIN) | build
	cat $^ > $@
	truncate -s 65536 $@   # pad to 128 sectors (boot + 127 kernel sectors)

tools/mkfs: tools/mkfs.c
	gcc -O2 -o $@ $<

build/hello.elf: user/hello.c user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/hello.c

build/echo.elf: user/echo.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/echo.c

build/cat.elf: user/cat.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/cat.c

build/grep.elf: user/grep.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/grep.c

build/head.elf: user/head.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/head.c

build/tail.elf: user/tail.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/tail.c

build/wc.elf: user/wc.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/wc.c

build/tee.elf: user/tee.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/tee.c

build/sort.elf: user/sort.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/sort.c

build/uniq.elf: user/uniq.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/uniq.c

build/tr.elf: user/tr.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/tr.c

build/cut.elf: user/cut.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/cut.c

build/seq.elf: user/seq.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/seq.c

build/hexdump.elf: user/hexdump.c user/user.h user/user.ld | build
	$(GCC) $(USER_CFLAGS) -T user/user.ld -o $@ user/hexdump.c

$(DISK_IMG): tools/mkfs build/hello.elf build/echo.elf build/cat.elf \
             build/grep.elf build/head.elf build/tail.elf build/wc.elf \
             build/tee.elf build/sort.elf build/uniq.elf build/tr.elf \
             build/cut.elf build/seq.elf build/hexdump.elf \
             user/greeting.txt | build
	tools/mkfs $@ \
	    d:/bin d:/home \
	    build/hello.elf:/bin/hello \
	    build/echo.elf:/bin/echo \
	    build/cat.elf:/bin/cat \
	    build/grep.elf:/bin/grep \
	    build/head.elf:/bin/head \
	    build/tail.elf:/bin/tail \
	    build/wc.elf:/bin/wc \
	    build/tee.elf:/bin/tee \
	    build/sort.elf:/bin/sort \
	    build/uniq.elf:/bin/uniq \
	    build/tr.elf:/bin/tr \
	    build/cut.elf:/bin/cut \
	    build/seq.elf:/bin/seq \
	    build/hexdump.elf:/bin/hexdump \
	    user/greeting.txt:/home/greeting.txt

build:
	mkdir -p build

clean:
	rm -f build/*.o build/*.bin build/*.img build/*.elf
	rm -f tools/mkfs

run: $(OS_IMG) $(DISK_IMG)
	qemu-system-i386 \
	    -drive format=raw,file=$(OS_IMG) \
	    -drive format=raw,file=$(DISK_IMG),if=ide,index=1 \
	    -display curses \
	    -serial file:build/serial.log

run-debug: $(OS_IMG) $(DISK_IMG)
	qemu-system-i386 \
	    -drive format=raw,file=$(OS_IMG) \
	    -drive format=raw,file=$(DISK_IMG),if=ide,index=1 \
	    -display none \
	    -serial stdio

.PHONY: all clean run run-debug disk
