#include "thread.h"

#include <stdint.h>

#include <kinterface.h>

#include "cmd_registry.h"
#include "err.h"
#include "syscall.h"
#include "test.h"

#define STACK_SIZE 1024

void thread(void *args)
{
	int *i = args;
	(*i)++;
}

void cmd_thread_test([[maybe_unused]] int argc,
		     [[maybe_unused]] char *const argv[])
{
	int i = 0;

	char stack[STACK_SIZE];
	thread_entry_t entry;
	entry.fn = thread;
	entry.args = &i;

	int8_t tid = thread_create(&entry, &stack[STACK_SIZE]);
	if (tid < 0) {
		exit_group(1);
	}

	thread_join(tid);

	TEST_ASSERT_EQ(1, i);
	TEST_SUCCESS();
}

#define MMAP_TEST_FILE "/path/to/test"

struct mmap_thread_args {
	volatile char *main_map; ///< Mapped by the main thread.
	volatile char *thread_map; ///< Mapped by the thread.
	int fd;
	char first;
};

static void mmap_thread(void *args)
{
	struct mmap_thread_args *a = args;

	// First touch from the thread: the fault fills the shared tree.
	a->first = a->main_map[0];
	a->main_map[1] = 't';

	void *va = mmap(0, 4096, PROT_READ, MAP_PRIVATE, a->fd, 0);
	a->thread_map = IS_ERR(va) ? NULL : va;
}

void cmd_mmap_thr_test([[maybe_unused]] int argc,
		       [[maybe_unused]] char *const argv[])
{
	int fd = open(MMAP_TEST_FILE, O_CREAT | O_W | O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}
	static char page[4096];
	memset(page, 'T', sizeof(page));
	TEST_ASSERT_EQ(sizeof(page), write(fd, page, sizeof(page)));

	void *va = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (IS_ERR(va)) {
		TEST_ERROR("mmap failed with %ld", PTR_ERR(va));
	}

	static char stack[4096];
	struct mmap_thread_args args = { .main_map = va, .fd = fd };
	thread_entry_t entry = { .fn = mmap_thread, .args = &args };

	int8_t tid = thread_create(&entry, &stack[sizeof(stack)]);
	if (tid < 0) {
		TEST_ERROR("thread_create failed: %d", tid);
	}
	thread_join(tid);
	close(fd);

	// Same mm: the thread sees the main mapping and its CoW write...
	TEST_ASSERT_EQ('T', args.first);
	TEST_ASSERT_EQ('t', args.main_map[1]);

	// ...and the main thread sees the mapping the thread created.
	if (!args.thread_map) {
		TEST_ERROR("mmap from the thread failed");
	}
	TEST_ASSERT_EQ('T', args.thread_map[0]);

	TEST_SUCCESS();
}
