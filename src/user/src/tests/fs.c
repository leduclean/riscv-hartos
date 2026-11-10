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
