# AliOS

A 32-bit x86 hobby operating system written in C and NASM assembly.

## Features

- Higher-half kernel mapped at `0xC0000000`
- Preemptive round-robin scheduler with ring-0 kernel tasks and ring-3 user tasks
- `fork()` / `exec()` / `wait()` / `kill()` process model
- Copy-on-Write virtual memory — pages are shared after `fork()` and copied lazily on first write
- Physical memory manager with reference counting
- AliFS — a simple flat filesystem stored on an ATA PIO slave disk
- ELF32 binary loader
- Interactive shell with history and line editing
- COM1 serial debug output at 115200 baud

## Requirements

- `nasm`
- `gcc` with 32-bit multilib support (`gcc-multilib`)
- `ld` (GNU binutils, 32-bit)
- `qemu-system-i386`
- `make`

On Debian/Ubuntu:
```
sudo apt install nasm gcc-multilib binutils qemu-system-x86
```

## Building

```
make clean && make
```

Produces:
- `build/os.img` — bootable kernel image (boot sector + kernel binary)
- `build/disk.img` — AliFS data disk containing user programs

## Running

```
make run
```

Launches QEMU with a curses VGA display. Serial output is logged to `build/serial.log`.

For headless mode with serial output to stdout:
```
make run-debug
```

## Shell commands

| Command | Description |
|---|---|
| `help` | Show available commands |
| `clear` | Clear the screen |
| `ls` | List files on disk |
| `run <name>` | Load and run an ELF binary from disk |
| `rm <name>` | Delete a file from disk |
| `cp <src> <dst>` | Copy a file on disk |
| `mem` | Show free physical memory |
| `shutdown` | Power off (QEMU ACPI) |
| `reboot` | Reboot the machine |

## Project layout

```
boot/          Boot sector (16-bit real mode → 32-bit protected mode)
kernel/        Kernel source
  serial.c/h   COM1 UART driver
  vga.c/h      VGA text mode output
  gdt.c/h      Global Descriptor Table + TSS
  idt.c/h      Interrupt Descriptor Table
  isr.asm      ISR stubs (pusha/popa frame)
  fault.c/h    CPU exception handlers + CoW page fault resolution
  irq.c/h      PIC 8259 IRQ dispatch
  timer.c/h    PIT 100 Hz preemption tick
  keyboard.c/h PS/2 keyboard driver with Ctrl-C support
  pmm.c/h      Physical memory manager (bitmap + reference counts)
  paging.c/h   x86 paging setup
  heap.c/h     Kernel heap (kmalloc/kfree)
  ata.c/h      ATA PIO disk read/write
  alfs.c/h     AliFS filesystem (read, write, delete)
  elf.c/h      ELF32 loader
  task.c/h     Task/PCB structure
  task_switch.asm  Context switch (saves/restores ESP, swaps CR3)
  sched.c/h    Scheduler (sleep, block, wake, exit, wait, signals)
  uvm.c/h      User virtual memory (create, map, clone CoW, free)
  user_enter.asm   iret into ring 3 (user_enter / user_enter_eax)
  syscall.c/h  int 0x80 syscall handler
  shell.c/h    Interactive shell
  kernel32.c   Kernel entry point (kmain)
user/          User-space programs
  hello.c      Example ELF binary
tools/         Host tools
  mkfs.c       Builds the AliFS disk image
```

## Syscall ABI

`int 0x80` — `eax` = syscall number, `ebx`/`ecx`/`edx` = arguments, `eax` = return value.

| # | Name | Arguments |
|---|---|---|
| 0 | `exit` | ebx = exit code |
| 1 | `write` | ebx = fd, ecx = buf, edx = len |
| 2 | `open` | ebx = name |
| 3 | `read` | ebx = fd, ecx = buf, edx = len |
| 4 | `close` | ebx = fd |
| 5 | `brk` | ebx = new break address |
| 6 | `sleep` | ebx = milliseconds |
| 7 | `exec` | ebx = name |
| 8 | `fork` | — (returns 0 to child, child pid to parent) |
| 9 | `wait` | ebx = pointer to exit code (int32_t) |
| 10 | `kill` | ebx = pid, ecx = signal |

## Memory layout

```
0x00000000 – 0x07FFFFFF   Identity map (supervisor only) — hardware access
0x08000000 – 0xBFFFFFFF   User address space (PDE[32..767])
0xC0000000 – 0xFFFFFFFF   Kernel (higher half)
```

## Running on real x86 hardware

Write the OS image to a USB drive and boot from it on any x86 PC:

```
dd if=build/os.img of=/dev/sdX bs=512
```

The kernel expects a BIOS boot environment and an E820 memory map. Serial output appears on COM1 at 115200 baud 8N1.
