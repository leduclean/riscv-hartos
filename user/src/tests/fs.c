/**
 * @file
 * @brief File system tests: open, read, write, seek, close.
 */

#include <stdint.h>

#include <kinterface.h>
#include <string.h>

#include "cmd_registry.h"
#include "err.h"
#include "syscall.h"
#include "test.h"

#define FS_TEST_FILE "/path/to/test"

void cmd_fs_io_test([[maybe_unused]] int argc,
		    [[maybe_unused]] char *const argv[])
{
	const char writen[] = "test\n";
	char readed[sizeof(writen)];

	int fd = open(FS_TEST_FILE, O_CREAT | O_W | O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}

	TEST_ASSERT_EQ(sizeof(writen), write(fd, writen, sizeof(writen)));

	TEST_ASSERT_EQ(0, seek(fd, 0, SEEK_SET));
	TEST_ASSERT_EQ(sizeof(readed), read(fd, readed, sizeof(readed)));
	TEST_ASSERT_EQ_STR(writen, readed);

	TEST_SUCCESS();

	close(fd);
}

void cmd_fs_close_test([[maybe_unused]] int argc,
		       [[maybe_unused]] char *const argv[])
{
	int8_t res;
	char sink[10];

	res = close(10);
	if (res == 0) {
		TEST_ERROR("close on unopened fd succeeded");
	}

	TEST_ASSERT_EQ(0, close(STDIN));

	res = read(STDIN, sink, sizeof(sink));
	if (res == 0) {
		TEST_ERROR("read on closed stdin returned 0");
	}

	TEST_SUCCESS();
}

void cmd_fs_dup2_test([[maybe_unused]] int argc,
		      [[maybe_unused]] char *const argv[])
{
	int fd_src = open(FS_TEST_FILE, O_CREAT | O_W | O_R);
	if (fd_src < 0) {
		TEST_ERROR("open src failed: %d", fd_src);
	}

	int fd_dst = NOFILE - 1;
	int res = dup2(fd_src, fd_dst);
	if (res < 0) {
		TEST_ERROR("Dup2 has failed with %d", res);
	}

	char test_buf[] = "Test";
	ssize_t written = write(fd_src, test_buf, sizeof(test_buf));
	if (written < 0) {
		TEST_ERROR("Write failed: %ld", written);
	}

	int64_t off = seek(fd_dst, 0, SEEK_SET);
	if (off < 0) {
		TEST_ERROR("seek 0 SET failed: %ld", off);
	}

	char readed_buf[sizeof(test_buf)] = { 0 };
	ssize_t readed = read(fd_dst, readed_buf, sizeof(readed_buf));
	if (readed < 0) {
		TEST_ERROR("Read failed: %ld", readed);
	}

	TEST_ASSERT_EQ_STR(test_buf, readed_buf);

	close(fd_src);
	close(fd_dst);

	TEST_SUCCESS();
}

void cmd_fs_pipe_test([[maybe_unused]] int argc,
		      [[maybe_unused]] char *const argv[])
{
	int fildes[2];
	int res = pipe(fildes);
	if (res < 0) {
		TEST_ERROR("Pipe has failed with %d", res);
	}

	char test_buf[] = "Test";
	int pid1, pid2;
	pid1 = fork();
	switch (pid1) {
	case -1:
		TEST_ERROR("fork failed");
	case 0:
		// Child 1:
		close(fildes[0]);

		// Writer of the pipe.
		dup2(fildes[1], STDOUT);
		close(fildes[1]);
		write(STDOUT, test_buf, sizeof(test_buf) - 1);
		break;
	default:
		pid2 = fork();
		switch (pid2) {
		case -1:
			TEST_ERROR("fork failed");
		case 0:
			// Child 2:
			close(fildes[1]);

			// Reader of the pipe.
			dup2(fildes[0], STDIN);
			close(fildes[0]);
			char readed_buf[sizeof(test_buf)] = { 0 };
			ssize_t readed = read(STDIN, readed_buf,
					      sizeof(test_buf) - 1);
			if (readed < 0) {
				TEST_ERROR("Read failed: %ld", readed);
			}
			TEST_ASSERT_EQ_STR(test_buf, readed_buf);
			TEST_SUCCESS();
			break;
		default:
			close(fildes[0]);
			close(fildes[1]);
			waitpid(pid1);
			waitpid(pid2);
			break;
		}
	}
}

void cmd_fs_seek_test([[maybe_unused]] int argc,
		      [[maybe_unused]] char *const argv[])
{
	int64_t off;

	int fd = open(FS_TEST_FILE, O_CREAT | O_W | O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}

	TEST_ASSERT_EQ(-EINVAL, seek(fd, -10, SEEK_SET));
	TEST_ASSERT_EQ(10, seek(fd, 10, SEEK_SET));
	TEST_ASSERT_EQ(0, seek(fd, -10, SEEK_CUR));
	TEST_ASSERT_EQ(-EINVAL, seek(fd, -10, SEEK_CUR));

	off = seek(fd, 10, SEEK_END);
	if (off < 0) {
		TEST_ERROR("seek 10 END failed: %ld", off);
	}

	TEST_SUCCESS();
}
void cmd_mmap_test([[maybe_unused]] int argc,
		   [[maybe_unused]] char *const argv[])
{
	int fd = open(FS_TEST_FILE, O_CREAT | O_W | O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}

	// Pages fully past EOF fault (POSIX SIGBUS): back the 3 pages with data.
	static char page[4096];
	for (int i = 0; i < 3; i++) {
		memset(page, '0' + i, sizeof(page));
		TEST_ASSERT_EQ(sizeof(page), write(fd, page, sizeof(page)));
	}

	size_t size = 3 * 4096;
	void *va = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (IS_ERR(va)) {
		TEST_ERROR("mmap failed with %lu", PTR_ERR(va));
	}
	close(fd);

	// Faults must load the file content
	char *ptr = (char *)va;
	if (ptr[0] != '0' || ptr[4096] != '1' || ptr[8192] != '2') {
		TEST_ERROR("Memory content mismatch with the file");
	}

	// Multipage writing
	ptr[0] = 'A';
	ptr[4096] = 'B';
	ptr[8192] = 'C';

	if (ptr[0] != 'A' || ptr[4096] != 'B' || ptr[8192] != 'C') {
		TEST_ERROR("Memory content mismatch after page faults");
	}

	// Invalid munmap
	int res = munmap((void *)0x7fffffff0000, 4096);
	if (res >= 0) {
		TEST_ERROR("munmap succeeded on invalid address (should fail)");
	}

	res = munmap(va, size);
	if (res < 0) {
		TEST_ERROR("munmap failed with %d", res);
	}

	// ptr[0] = 'X';

	TEST_SUCCESS();
}

/*
 * The kernel never syncs the block cache to the disk yet: the MAP_SHARED
 * tests read the file back within the same boot, through the inode.
 */

/** @brief Read @p n bytes of FS_TEST_FILE at @p off into @p buf. */
static void read_test_file_at(int64_t off, char *buf, size_t n)
{
	int fd = open(FS_TEST_FILE, O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}
	TEST_ASSERT_EQ(off, seek(fd, off, SEEK_SET));
	TEST_ASSERT_EQ(n, read(fd, buf, n));
	close(fd);
}

/** @brief Fill the first @p npages of FS_TEST_FILE with '0', '1', ... */
static void fill_test_file(int npages)
{
	int fd = open(FS_TEST_FILE, O_CREAT | O_W | O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}

	static char page[4096];
	for (int i = 0; i < npages; i++) {
		memset(page, '0' + i, sizeof(page));
		TEST_ASSERT_EQ(sizeof(page), write(fd, page, sizeof(page)));
	}
	close(fd);
}

/** @brief Map the first @p len bytes of FS_TEST_FILE, shared and writable. */
static char *map_test_file_shared(size_t len)
{
	int fd = open(FS_TEST_FILE, O_W | O_R);
	if (fd < 0) {
		TEST_ERROR("open failed: %d", fd);
	}

	void *va = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (IS_ERR(va)) {
		TEST_ERROR("mmap failed with %ld", PTR_ERR(va));
	}
	// The mapping holds its own reference on the file.
	close(fd);
	return va;
}

void cmd_mmap_shr_test([[maybe_unused]] int argc,
		       [[maybe_unused]] char *const argv[])
{
	fill_test_file(2);
	char *ptr = map_test_file_shared(2 * 4096);

	// Page 0 is written straight away, page 1 is read before.
	ptr[0] = 'A';
	TEST_ASSERT_EQ('1', ptr[4096]);
	ptr[4096 + 1] = 'B';

	TEST_ASSERT_EQ(0, munmap(ptr, 2 * 4096));

	char buf[2];
	read_test_file_at(0, buf, sizeof(buf));
	TEST_ASSERT_EQ('A', buf[0]);
	TEST_ASSERT_EQ('0', buf[1]);

	read_test_file_at(4096, buf, sizeof(buf));
	TEST_ASSERT_EQ('1', buf[0]);
	TEST_ASSERT_EQ('B', buf[1]);

	TEST_SUCCESS();
}

void cmd_mmap_exit_test([[maybe_unused]] int argc,
			[[maybe_unused]] char *const argv[])
{
	fill_test_file(1);

	int8_t pid = fork();
	if (pid < 0) {
		TEST_ERROR("fork failed: %d", pid);
	}

	if (pid == 0) {
		// No munmap: the write back must happen when the mm dies.
		char *ptr = map_test_file_shared(4096);
		ptr[0] = 'C';
		exit(0);
	}

	waitpid(pid);

	char c;
	read_test_file_at(0, &c, 1);
	TEST_ASSERT_EQ('C', c);

	TEST_SUCCESS();
}
