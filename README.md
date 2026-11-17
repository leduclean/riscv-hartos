# HartOS

**A small RISC-V kernel that does things the way Linux does them.**

Copy-on-write `fork`, threads sharing an address space, file-backed `mmap`,
a priority scheduler and virtio block I/O, in plain, readable C. Boots on QEMU `virt`, no libc, no host OS: everything from the trap
vector to the shell is in this repo.

---

## Why not just xv6?

[xv6](https://github.com/mit-pdos/xv6-riscv) is the reference for learning
Unix v6. HartOS picks up where it stops, with the mechanisms a modern
kernel actually relies on.

| | xv6-riscv | HartOS |
| --- | --- | --- |
| `fork` | full copy | **copy-on-write**, refcounted pages |
| Threads | no | **`clone(CLONE_VM)`**, thread groups, `exit_group` |
| `mmap` / `munmap` | no | **file-backed, `MAP_SHARED` write-back** |
| Scheduler | round-robin | **priority classes**, preemption control |
| Sync | spinlock, sleeplock | spinlock, **mutex, semaphore, wait queues with timeout** |
| Debug checks | no | **`might_sleep()`** in atomic context |
| Console | UART | UART + **framebuffer** |
| Block I/O | virtio | virtio |
| SMP | yes | in progress |

---

## Read the book, then the code

The kernel follows
[*Operating Systems: Three Easy Pieces*](https://pages.cs.wisc.edu/~remzi/OSTEP/).
Each concept maps to one place in the source:

| OSTEP | Concept | Code |
| --- | --- | --- |
| Virtualization | Processes, `fork` / `exec` / `clone` | [`task.c`](kernel/core/task/task.c) |
| | Scheduling | [`scheduler.c`](kernel/core/sched/scheduler.c) |
| | Paging (Sv39), copy-on-write | [`vpages.c`](kernel/core/memory/vpages.c), [`trap.c`](arch/trap.c) |
| | `mmap` | [`mmap.c`](kernel/core/memory/mmap.c) |
| Concurrency | Locks, semaphores, wait queues | [`sync/`](kernel/core/sync) |
| | Threads | [`thread.c`](user/lib/thread.c) |
| Persistence | Block device | [`virtio_blk.c`](kernel/drivers/blk/virtio_blk.c) |
| | Buffer cache, inodes, files, pipes | [`fs/`](kernel/core/fs) |

---

## Quick start

Requires `riscv64-elf-gcc`, `qemu-system-riscv64`, `cmake` and
[`uv`](https://github.com/astral-sh/uv) for the tests.

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=riscv64-toolchain.cmake
cmake --build build --target run      # serial console
cmake --build build --target rung     # framebuffer console
```

In the shell, `help` lists the built-in programs (CoW fork, threads, pipes,
`mmap`, segfault isolation…).

**Tests**: `cmake --build build --target integration` boots the kernel in QEMU
and drives every shell program from pytest.

**Debug**: `--target debug` starts a GDB server on `:1234`, then `bash debug.sh`.

---

## Roadmap

- [ ] **SMP**: per-hart state, scheduler lock handoff, IPIs, TLB shootdown
- [ ] User utilities (`ls`, `cat`, `echo`, `ps`) and shell pipes / redirections
- [ ] Design notes per subsystem, each mapped to its Linux counterpart
