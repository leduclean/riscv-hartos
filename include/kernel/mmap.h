#pragma once
/**
 * @file
 * @brief This file give the interface for the sv39 virtual paging.
 */

#include <stddef.h>
#include <stdint.h>

#include <lib/clist.h>

#include <kernel/task.h>

/**
 * @class vmap
 * @brief Virtual user maping used to keep track of the mmap elements 
 * of a task.
 *
 */
struct vmap {
	clist_node_t node;

	uintptr_t vm_start;
	uintptr_t vm_end;
	uint16_t flags;
	uint16_t prot;
	struct file *f;
};

extern char _trampoline_start[], _trampoline_end[];

/**
 * @brief Allocate the kernel page table tree and switch to kernel page table.
 * 
 * @note The corresponding satp is accessible via @ref mmap_kernel_satp.
 * @retval 0 Success.
 * @retval -ENOMEM Out of memory.
 */
int mmap_kernel(void);

/**
 * @brief Maps IO memory mapped address into the kernel.
 *
 * @param addr Addr of the memory mapped address space.
 * @param size The size of the mapped address.
 * @retval 0 Success.
 * @retval -ENOMEM Out of memory.
 */
int iommap_kernel(void *addr, size_t size);

/**
 * @brief Memory map a file @p f.
 *
 * @param p Task pcb.
 * @param va TODO: For now va is ignored and a random free va is given.
 * @param f File to map to a va.
 * @param len Size of the file to mmap.
 * @param prot Protections on the mapping, see PROT_X.
 * @param flags Memory mapping policy, see MAP_X.
 * @retval -EINVAL if len is zero.
 * @retval -EMFILE if they are too many mapping.
 * @retval -ENOMEM if it's impossible to allocate a mapping. 
 * @retval -EBDAF if file is not supplied.
 * @retval -EACCES if there is flags/prot/files incoherency.
 */
intptr_t mmap_file(task_t *p, [[maybe_unused]] uintptr_t va, struct file *f,
		   size_t len, uint16_t prot, uint16_t flags);

/**
 * @brief Unmap regions between [va, va + len] page aligned.
 *
 * @param p Task pcb.
 * @param va Virtual base adress for the unmap
 * @param len Size of the regions range to unmap.
 * @retval 0 Success.
 * @retval -EINVAL if this dosn't match any mapped region.
 * @retval -EINVAL if len = 0
 */
int mmap_unmap(task_t *p, uintptr_t va, size_t len);

/**
 * @brief Resolve a virtual address mapping.
 *
 * @param p Task pcb.
 * @param va Virtual adress of the address that has faulted.
 * @return -EINVAL if the maping has no file
 * @return -ENOENT if no corresponding mapping.
 * @return -ENOMEM if there is no sufficiant memory to lazy allocate a page on the fault.
 */
int mmap_resolve_va(task_t *p, uintptr_t va);

/**
 * @brief Allocate an empty address space (root page table, no mapping).
 *
 * @return The new mm with one user, NULL if out of memory.
 */
struct mm *mm_alloc(void);

/**
 * @brief Take a reference on an address space (CLONE_VM).
 *
 * @return @p mm.
 */
struct mm *mm_get(struct mm *mm);

/**
 * @brief Drop a reference on an address space.
 *
 * The last user writes back the MAP_SHARED mappings, releases every vmap and
 * frees the whole page tree. A failed write back is only reported: nobody is
 * left to receive the error.
 */
void mm_put(struct mm *mm);

/**
 * @brief Allocate and map what each user task owns (root pd and a trapframe).
 *
 * @note fork stops here. The tree owns the trapframe too:
 * mm_put(p->mm) frees everything.
 *
 * @param p A pointer to the owner task.
 * @retval 0 Success.
 * @retval -EINVAL @p p is not a user task.
 * @retval -ENOMEM Out of memory.
 */
int mmap_uspace(task_t *p);

/**
 * @brief mmap_uspace, code, trampoline and stack. The no parent case.
 *
 * @note The tree owns the trapframe on sucess.
 *
 * @warning On failure nothing stays allocated, you don't need 
 * to free the tree by yourself.
 *
 * @param p A pointer to the owner task.
 * @retval 0 Success.
 * @retval -Exxx See mmap_uspace().
 */
int mmap_uimage(task_t *p);

/**
 * @brief Get the kernel satp. 
 *
 * @warning @ref mmap_kernel must have been called before.
 * or you get a random state.
 */
unsigned long mmap_kernel_satp(void);

/**
 * @brief Switch to the kernel ptable updating the satp reg.
 */
void mmap_switch_to_kernel(void);
/**
 * @brief Get satp encoded value from a physical page pointer
 *
 * @param pt A physical page pointer.
 */
unsigned long mmap_satp(void *pt);
