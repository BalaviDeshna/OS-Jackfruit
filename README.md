# Multi-Container Runtime

A lightweight Linux container runtime in C with a long-running supervisor and a kernel-space memory monitor.

## Team Information

| Name             | SRN           |
| ---------------- | ------------- |
| K B BALAVIDESHNA | PES1UG24CS206 |
| KENCHE SRUJANA   | PES1UG24CS227 |

## 2. Build, Load, and Run Instructions

### Prerequisites

* Ubuntu 22.04 or 24.04 (bare metal or VM)
* Secure Boot **OFF** (required for kernel module loading)
* Linux kernel headers installed

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

### Setup

```bash
git clone https://github.com/<your-username>/OS-Jackfruit.git
cd OS-Jackfruit
sudo tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base
sudo tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-alpha
sudo tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-beta
```

### Build

```bash
cd boilerplate
sudo make
cp cpu_hog memory_hog io_pulse ../rootfs-alpha/
cp cpu_hog memory_hog io_pulse ../rootfs-beta/
```

### Load Module

```bash
sudo insmod monitor.ko
ls -l /dev/container_monitor   # verify device exists
dmesg | tail                   # verify "Module loaded"
```

### Run

```bash
# Terminal 1 - start supervisor
sudo ./engine supervisor ../rootfs-base
 
# Terminal 2 - launch containers
sudo ./engine start alpha /path/to/rootfs-alpha /bin/sleep 100
sudo ./engine start beta  /path/to/rootfs-beta  /bin/sleep 100
sudo ./engine ps
sudo ./engine logs alpha
sudo ./engine stop alpha
 
# Run in foreground
sudo ./engine run test /path/to/rootfs-alpha /bin/ls
```

### Cleanup

```bash
sudo ./engine stop alpha
sudo ./engine stop beta
# Ctrl+C supervisor
sudo rmmod monitor
dmesg | tail   # verify "Module unloaded"
```

---

## 3. Demo Screenshots

| #   | What                        | Screenshot                    |
| --- | --------------------------- | ----------------------------- |
| 1   | Multi-container supervision | ![os1](screenshots/os1.png) |
| 2   | Metadata tracking           | ![os2](screenshots/os2.png) |
| 3   | Bounded-buffer logging      | ![os3](screenshots/os3.png) |
| 4   | CLI and IPC                 | ![os4](screenshots/os4.png) |
| 5   | Soft-limit warning          | ![os5](screenshots/os5.png) |
| 6   | Hard-limit enforcement      | ![os6](screenshots/os6.png) |
| 7   | Scheduling experiment       | ![os7](screenshots/os7.png) |
| 8   | Clean teardown              | ![os8](screenshots/os8.png) |

---

## 4. Engineering Analysis

### Isolation Mechanisms

The runtime uses `clone()` with `CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS`. Each container gets its own PID namespace (sees itself as PID 1), its own hostname, and its own mount namespace. `chroot()` locks the container into the Alpine rootfs. `/proc` is mounted inside the container after chroot so commands like `ps` work. The host kernel is still shared - all syscalls go through it and the container's PID is visible to the host.

### Supervisor and Process Lifecycle

The long-running supervisor is needed to reap exited containers. Without a parent calling `waitpid`, exited containers become zombies. The supervisor installs a `SIGCHLD` handler that calls `waitpid(-1, WNOHANG)` to reap all exited children and update their metadata state. A linked list of `container_record_t` nodes tracks each container's ID, PID, state, memory limits, and exit status.

### IPC, Threads, and Synchronization

Two IPC mechanisms are used. A pipe per container captures stdout/stderr - a producer thread reads from the pipe and pushes into a bounded buffer, and a logging thread pops and writes to log files. A UNIX domain socket at `/tmp/mini_runtime.sock` handles CLI commands separately from logging.

The bounded buffer uses a mutex and two condition variables (`not_full`, `not_empty`). Without the mutex, concurrent producers could corrupt the same buffer slot. Condition variables avoid busy-waiting. The metadata list uses a separate mutex to avoid deadlock between the buffer lock and metadata lock.

### Memory Management and Enforcement

RSS measures physical RAM pages currently in use. It does not measure virtual memory, swap, or shared library pages. The soft limit logs a warning once when first exceeded. The hard limit sends `SIGKILL`. Enforcement is in kernel space because user-space polling can be delayed by the scheduler - a kernel timer fires every second regardless of user-space load.

### Scheduling Behavior

Linux CFS assigns CPU time proportional to priority weights derived from `nice` values. Lower nice = higher weight = more CPU time under contention. I/O-bound processes frequently block and yield the CPU, so CFS gives them a scheduling boost when they wake, making them more responsive despite sharing the CPU with CPU-bound processes.

---

## 5. Design Decisions and Tradeoffs

|Subsystem|Decision|Tradeoff|Justification|
|-|-|-|-|
|Namespace isolation|PID + UTS + mount only|No network isolation|Network isolation requires veth/bridge setup outside project scope|
|Supervisor architecture|Single-threaded event loop with `select`|CLI requests serialized|Kept it single-threaded to avoid race conditions in a demo|
|IPC/Logging|Pipe per container + UNIX socket|One producer thread per container|Clean separation between logging and control paths|
|Kernel monitor|Mutex over spinlock|Cannot be held in hard IRQ context|ioctl runs in process context; critical section is short, no sleeping|
|Scheduling experiments|`nice` values|Effect only visible under contention|Directly demonstrates CFS weight-based fairness|

---

## 6. Scheduler Experiment Results

### Experiment 1 - CPU-bound, different priorities

Two `cpu_hog` containers running simultaneously:

|Container|Nice| Completion Time |
|-|-|-|
|high|-5| ~9.120 sec      |
|low|+10| ~9.416 sec      |

The high-priority container finished faster. CFS weight ratio between nice -5 and nice +10 is ~3.5:1, so it received ~3.5x more CPU time under contention.

### Experiment 2 - CPU-bound vs I/O-bound

`cpu_hog` and `io_pulse` at the same priority. The I/O-bound container blocked frequently on I/O, allowing the CPU-bound container to run during those periods. CFS boosted the I/O container's priority on wakeup, keeping its latency low despite sharing CPU.

