#pragma once
#include <stdint.h>

#include <kernel/file.h>
#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/spinlock.h>
#include <kernel/waitqueue.h>

struct pipe {
	semaphore_t read_data; // Read data is left.
	semaphore_t write_data; // Write data location.

	// Lock protecting pipe info
	spinlock_t lock;
	char *buf;
	uint32_t head;
	uint32_t tail;
	uint32_t wcnt;
	uint32_t rcnt;
};

/**
 * @brief Alloc a new pipe structure.
 */
struct pipe *pipe_alloc(void);

/**
 * @brief Put a writer of the pipe.
 *
 * @param p Pointer to the pipe.
 */
void pipe_putw(struct pipe *p);

/**
 * @brief Put a reader of the pipe.
 *
 * @param p Pointer to the pipe.
 */
void pipe_putr(struct pipe *p);

/**
 * @brief Write into the pipe.
 *
 * @param f The file to write.
 * @param user Weither the write is from a user buffer or not.
 * @param src Source buffer.
 * @param count Number of bytes to write.
 *
 * @return Number of bytes read.
 * @retval -EINVAL Range past the end of file or @p count too big or file is not a pipe.
 */
ssize_t pipe_write(struct file *f, bool user, const char *src, size_t count);

/**
 * @brief Read up to count @count bytes from the @f content to the @dst.
 *
 * @param f The file to read.
 * @param user Weither the read is from a user buffer or not.
 * @param dst Destination buffer.
 * @param count number of bytes to read.
 *
 * @return Number of bytes read.
 * @retval -EINVAL Range past the end of file or @p count too big or file is not a pipe.
*/
ssize_t pipe_read(struct file *f, bool user, char *dst, size_t count);
