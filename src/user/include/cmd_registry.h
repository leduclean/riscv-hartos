/**
 * @file
 * @brief Registry of the commands the shell can run.
 */

#pragma once

#include <kinterface.h>

/**
 * @brief Command registry.
 *
 * To add a command: X(name, desc, usage) here, then define cmd_name().
 */
#define CMD_LIST(X)                                                           \
	X(help, "Show all the commands", "help")                              \
	X(segfault, "Segfault handling", "segfault")                          \
	X(stream, "Stream console input back with read/write", "stream_test") \
	X(sleep_test, "Sleep user system call test", "sleep_call")            \
	X(fork_test, "Test the page table copy with CoW", "fork_test")        \
	X(exec_test, "Exec the sleep_call app", "exec_test")                  \
	X(exec_success, "Exec image load by an exec test",                    \
	  "(standalone exec has no sense)")                                   \
	X(fork_exec_test, "Fork then exec in the child", "fork_exec_test")    \
	X(fs_io_test, "Read write roundtrip", "fs_io_test")                   \
	X(fs_close_test, "Test close behavior on a fd", "fs_close_test")      \
	X(fs_dup2_test, "Test dup2 behavior on a fd", "fs_dup2_test")         \
	X(fs_seek_test, "Test the seek syscall on a fd", "fs_seek_test")      \
	X(fs_pipe_test, "Test a pipe between 2 process", "fs_pipe_test")      \
	X(mmap_test, "Test the mmap behavior", "mmap_test")                   \
	X(mmap_shr_test, "MAP_SHARED write back on munmap", "mmap_shr_test")  \
	X(mmap_exit_test, "MAP_SHARED write back on exit", "mmap_exit_test")  \
	X(thread_test, "Test the thread api", "thread_test")

typedef void (*cmd_func_t)(int argc, char *const argv[]);

#define X_DECL(n, ...) void cmd_##n(int argc, char *const argv[]);
CMD_LIST(X_DECL)
#undef X_DECL

/**
 * @brief Find a command by name.
 *
 * @param name The command name.
 * @return The command, or NULL if unknown.
 */
cmd_func_t cmd_find(char *const name);
