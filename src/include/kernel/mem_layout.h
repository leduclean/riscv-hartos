/**
 * @file
 * @brief This file define the memory layout of the kernel.
 */

#pragma once

#define MAX_VA (1L << 38)

// Each page as a size of 4096 bytes
#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)

/**
 * @name User address space layout.
 *
 * @{
 */
#define TRAMPOLINE \
	(MAX_VA -  \
	 PAGE_SIZE) /**< Trampoline va, must be the same in the kernel. */
#define TRAPFRAME (MAX_VA - 2 * PAGE_SIZE) /**<Trapframe va.*/
#define USTACK TRAPFRAME /**< User stack va. */
#define USTACK_SIZE 1 /**< Ustack size in page */
#define USTACK_TOP (USTACK - USTACK_SIZE * PAGE_SIZE) /**< User stack top va. */
#define GUARD_PAGES \
	4 /**< Number of guard page for the stack overflow handling */
#define MMAP_BASE (USTACK_TOP + GUARD_PAGES) /**< VA for the user mmap. */
/** @} */
