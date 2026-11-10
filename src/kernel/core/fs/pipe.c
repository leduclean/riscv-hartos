#include <lib/err.h>
#include <lib/tinyalloc.h>

#include <arch/asm_defs.h>
#include <kernel/file.h>
#include <kernel/pages.h>
#include <kernel/pipe.h>
#include <kernel/semaphore.h>
#include <kernel/spinlock.h>
#include <kernel/syscall.h>
#include <kernel/waitqueue.h>

#define PIPE_SIZE PAGE_SIZE

struct pipe *pipe_alloc(void)
{
	struct pipe *p = (struct pipe *)ta_calloc(1, sizeof(*p));
	if (!p) {
		goto out;
	}

	void *buf = page_alloc();
	if (!buf) {
		goto err_free_p;
	}
	p->buf = buf;

	spinlock_init(&p->lock);
	sem_init(&p->read_data, 0);
	sem_init(&p->write_data, PIPE_SIZE);

	return p;

err_free_p:
	free(p);
out:
	return ERR_PTR(-ENOMEM);
}

static void checked_release_locked(struct pipe *p)
{
	if (p->wcnt == 0 && p->rcnt == 0) {
		page_put(p->buf);
		free(p);
	}
}

void pipe_putr(struct pipe *p)
{
	spinlock_lock(&p->lock);

	if (p->rcnt-- == 1) {
		checked_release_locked(p);
	}

	spinlock_unlock(&p->lock);
}

void pipe_putw(struct pipe *p)
{
	spinlock_lock(&p->lock);
	if (p->wcnt-- == 1) {
		checked_release_locked(p);
	}

	spinlock_unlock(&p->lock);
}

static bool is_full_locked(const struct pipe *p)
{
	return (p->tail + 1) % PIPE_SIZE == p->head;
}

static bool is_empty_locked(const struct pipe *p)
{
	return p->head == p->tail;
}

static void write_char(struct pipe *p, char c)
{
	sem_wait(&p->write_data);
	spinlock_lock(&p->lock);

	// guard
	if (is_full_locked(p)) {
		panic("Impossible pipe behavior");
	}

	p->buf[p->tail] = c;
	p->tail = (p->tail + 1) % PIPE_SIZE;

	spinlock_unlock(&p->lock);
	sem_post(&p->read_data);
}

static int read_char(struct pipe *p, char *c)
{
	if (sem_try_take(&p->read_data) == 0) {
		spinlock_lock(&p->lock);

		if (is_empty_locked(p)) {
			spinlock_unlock(&p->lock);
			panic("Impossible pipe behavior");
		}

		*c = p->buf[p->head];
		p->head = (p->head + 1) % PIPE_SIZE;

		spinlock_unlock(&p->lock);
		sem_post(&p->write_data);
		return 0;
	}

	spinlock_lock(&p->lock);
	if (p->wcnt == 0) {
		spinlock_unlock(&p->lock);
		return -ENOENT; // EOF
	}
	spinlock_unlock(&p->lock);

	sem_wait(&p->read_data);
	spinlock_lock(&p->lock);

	if (is_empty_locked(p)) {
		spinlock_unlock(&p->lock);
		panic("Impossible pipe behavior");
	}
	*c = p->buf[p->head];
	p->head = (p->head + 1) % PIPE_SIZE;
	spinlock_unlock(&p->lock);

	sem_post(&p->write_data);

	return 0;
}
ssize_t pipe_write(struct file *f, bool user, const char *src, size_t count)
{
	struct pipe *p = f->p;
	if (!p) {
		return -EINVAL;
	}

	char kbuf[SYS_BUFFER_SIZE];
	if (user) {
		if (count > SYS_BUFFER_SIZE) {
			return -EINVAL;
		}
		int res = vpage_copyin(task_active()->root_ptable, kbuf, src,
				       count);
		if (res < 0) {
			return res;
		}

		src = kbuf;
	}

	for (size_t i = 0; i < count; i++) {
		write_char(p, src[i]);
	}

	return 0;
}

ssize_t pipe_read(struct file *f, bool user, char *dst, size_t count)
{
	struct pipe *p = f->p;
	if (!p) {
		return -EINVAL;
	}

	char kbuf[SYS_BUFFER_SIZE];
	if (user && count > SYS_BUFFER_SIZE) {
		return -EINVAL;
	}

	char *buf = user ? kbuf : dst;
	for (size_t i = 0; i < count; i++) {
		int res = read_char(f->p, &buf[i]);
		if (res < 0) {
			// EOF
			count = i;
			break;
		}
	}

	if (user) {
		int res = vpage_copyout(task_active()->root_ptable, dst, buf,
					count);
		if (res < 0) {
			return res;
		}
	}

	return count;
}
