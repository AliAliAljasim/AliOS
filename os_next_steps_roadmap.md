# Operating System Development Roadmap (Post-Core OS)

## Tier 1 --- Foundation for Real Programs

### 1. More Syscalls + File Descriptors

Add a basic POSIX-style file descriptor interface.

**Syscalls to implement** - open() - read() - write() - close() - brk()
/ sbrk()

**Purpose** Right now userspace programs can only print and exit. File
descriptors allow real programs to interact with files and devices.

Architecture flow:

open("hello.txt") → VFS Layer → AliFS driver → Disk access

------------------------------------------------------------------------

### 2. Blocking / Sleep Primitives

Introduce proper task blocking so the scheduler can suspend tasks.

Required changes: - TASK_BLOCKED state - sched_wake() -
sys_sleep(milliseconds)

Without blocking tasks waste CPU cycles with busy loops.

------------------------------------------------------------------------

### 3. wait() / Process Reaping

Allow parent processes to wait for children.

Syscall: wait(pid)

Behavior: Parent blocks until the child exits, preventing zombie
processes.

Example:

shell └── run program └── exit shell resumes after wait()

------------------------------------------------------------------------

# Tier 2 --- Process Model

### 4. fork() + exec()

This is the biggest jump in OS sophistication.

fork(): Creates a duplicate process.

parent process → fork() → parent + child processes

exec(): Replaces the current process with a new ELF program.

Typical shell workflow:

fork() → child → exec(program) → parent → wait()

------------------------------------------------------------------------

### 5. Signals

Minimum signals: - SIGKILL - SIGTERM

Allows the shell to terminate runaway tasks.

Example:

Ctrl+C → shell sends SIGTERM → program terminates

------------------------------------------------------------------------

# Tier 3 --- Quality of Life

### 6. Serial Port Debug Output

Add COM1 UART debugging.

Benefits: - kernel logs outside VGA - scrollback history - easier
debugging

Example:

vga_puts("Kernel panic") serial_puts("Kernel panic")

Run QEMU with:

qemu-system-x86_64 -serial stdio

------------------------------------------------------------------------

### 7. ATA Write + AliFS Write Support

Add disk write support.

Enables commands like:

cp rm touch

Also allows persistent storage across reboots.

------------------------------------------------------------------------

### 8. ACPI Shutdown / Reboot

Implement clean shutdown command.

shutdown

QEMU shortcut:

outw(0x604, 0x2000);

------------------------------------------------------------------------

# Tier 4 --- Advanced

### 9. Demand Paging / Copy-On-Write

Lazy memory allocation:

map virtual page → first access → page fault → allocate physical page

Benefits: - lower memory usage - efficient fork()

------------------------------------------------------------------------

### 10. Improved Filesystem

Option A: Extend AliFS - directories - metadata - hierarchical structure

Example:

/ ├── bin ├── home │ └── user └── dev

Option B: Add FAT12 filesystem support.

------------------------------------------------------------------------

### 11. Networking

Add NIC driver.

QEMU provides: - RTL8139 - e1000

Approx driver size: \~500 lines.

Networking stack layers:

Ethernet → IP → TCP / UDP → Sockets API

Enables: - ping - web servers - remote shells

------------------------------------------------------------------------

# Development Progression

Syscalls + File Descriptors → Blocking + wait() → fork + exec → Signals
→ Filesystem writes → Demand paging → Better filesystem → Networking

At this stage the OS resembles a small UNIX-like operating system.
