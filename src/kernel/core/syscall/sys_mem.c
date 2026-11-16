#include <stdint.h>

#include <lib/err.h>

#include <kernel/mmap.h>
#include <kernel/task.h>

#include "kernel/syscall.h"

void *sys_mmap([[maybe_unused]] void *addr, size_t len, int prot, int flags,
	       int fildes, [[maybe_unused]] int64_t off)
{
	task_t *active = task_active();
	struct file *f = task_get_fd(active, fildes);
	if (IS_ERR(f)) {
		return f;
	}

	return (void *)mmap_file(active, 0, f, len, prot, flags);
}

int sys_munmap(void *addr, size_t len)
{
	task_t *active = task_active();
	return mmap_unmap(active, (uintptr_t)addr, len);
}
