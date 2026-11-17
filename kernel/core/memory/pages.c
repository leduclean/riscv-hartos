#include <stddef.h>
#include <stdint.h>

#include <lib/clist.h>
#include <lib/stdio.h>

#include <arch/asm_defs.h>
#include <arch/cpu.h>
#include <kernel/ldsym.h>
#include <kernel/pages.h>
#include <kernel/spinlock.h>

/**
 * @typedef page
 * @brief Physical page.
 *
 * @note The idiom is that the physical frame contain an initialized node
 * when free and is dirty when allocated.
 */
typedef struct page {
	clist_node_t node; //< Clist node for free list.
} page_t;

/**
 * @brief Safety: this lock protects the whole page allocator list.
 */
static spinlock_t lock = SPINLOCK_UNLOCKED;
static clist_node_t free_pages;
// Keep in sync with MEMORY in kernel.ld (checked by page_init()).
#define RAM_SIZE (128UL * 1024 * 1024)
/**
 * @brief Ref counter of every ram page, indexed by page_idx().
 *
 * @note Sized on the whole ram (a compile-time constant): the entries of
 * the kernel image pages are never used.
 */
static uint8_t page_rc[RAM_SIZE >> PAGE_SHIFT] = { 0 };

static inline void push_free_locked(void *page)
{
	page_t *p = page;
	clist_init_node(&p->node);
	// LIFO
	clist_push_front(&free_pages, &p->node);
}

static inline size_t page_idx_locked(const void *page)
{
	return ((uintptr_t)page - (uintptr_t)_ram_start) >> PAGE_SHIFT;
}

static inline uint8_t page_get_refcnt_locked(const void *page)
{
	return page_rc[page_idx_locked(page)];
}

uint8_t page_get_refcnt(const void *page)
{
	spinlock_lock(&lock);

	uint8_t refcnt = page_get_refcnt_locked(page);

	spinlock_unlock(&lock);

	return refcnt;
}

static inline void *page_get_locked(void *page)
{
	page_rc[page_idx_locked(page)]++;
	return (void *)page;
}

void *page_get(void *page)
{
	spinlock_lock(&lock);

	void *p = page_get_locked(page);

	spinlock_unlock(&lock);
	return p;
}

void page_init(void)
{
	if ((uintptr_t)(_ram_end - _ram_start) != RAM_SIZE) {
		panic("pages.c RAM_SIZE out of sync with kernel.ld");
	}

	clist_init_node(&free_pages);

	spinlock_lock(&lock);

	for (char *p = _page_start; p < _page_end; p += PAGE_SIZE) {
		push_free_locked(p);
	}

	spinlock_unlock(&lock);
}

void *page_alloc(void)
{
	spinlock_lock(&lock);

	void *addr = clist_pop_front(&free_pages);
	if (!addr) {
		goto err_unlock;
	}

	memset(addr, 0, PAGE_SIZE);
	page_get_locked(addr);

err_unlock:
	spinlock_unlock(&lock);
	return addr;
}

void page_put(void *page)
{
	spinlock_lock(&lock);

	if (page_get_refcnt_locked(page) == 0) {
		panic("page_put() on a page nobody holds: double free");
	}
	if (--page_rc[page_idx_locked(page)] == 0) {
		push_free_locked(page);
	}

	spinlock_unlock(&lock);
}
