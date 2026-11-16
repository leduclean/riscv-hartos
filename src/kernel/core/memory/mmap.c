#include <stdatomic.h>
#include <stdint.h>

#include <lib/bit.h>
#include <lib/clist.h>
#include <lib/container.h>
#include <lib/err.h>
#include <lib/minmax.h>
#include <lib/stdio.h>
#include <lib/tinyalloc.h>

#include <arch/asm_defs.h>
#include <arch/board.h>
#include <arch/cpu.h>
#include <arch/csr.h>
#include <arch/mmu.h>
#include <arch/trap.h>
#include <kernel/bcache.h>
#include <kernel/file.h>
#include <kernel/fs_layout.h>
#include <kernel/inode.h>
#include <kernel/ldsym.h>
#include <kernel/mem_layout.h>
#include <kernel/mmap.h>
#include <kernel/mutex.h>
#include <kernel/pages.h>
#include <kernel/task.h>
#include <kernel/vpages.h>
#include <user/kinterface.h>

static pte_t *kroot;

#define SATP_SV39_MODE 8UL
#define SATP_MODE_SHIFT 60
#define SATP_MODE_MASK (0xfUL << SATP_MODE_SHIFT)

unsigned long mmap_satp(void *pt)
{
	return ((SATP_SV39_MODE << SATP_MODE_SHIFT) | page_get_num(pt));
}

unsigned long mmap_kernel_satp(void)
{
	return mmap_satp(kroot);
}

void mmap_switch_to_kernel(void)
{
	csr_write(satp, mmap_kernel_satp());
	mmu_flush_tlb();
}

static struct vmap *vmap_from_node(clist_node_t *node)
{
	return container_of(node, struct vmap, node);
}

static void free_vmap(struct vmap *m)
{
	file_put(m->f);
	ta_free(m);
}

static int write_back(pte_t *root, struct vmap *m)
{
	if (!(m->flags & MAP_SHARED)) {
		panic("Mmap write back should only be called with MAP_SHARED policy.");
	}

	int res;
	struct file *f = m->f;
	res = inode_lock(f->i);
	if (res < 0) {
		return res;
	}

	bool written_back = false;
	for (size_t off = 0;
	     off < MIN(f->i->content.size, m->vm_end - m->vm_start);
	     off += PAGE_SIZE) {
		size_t n = MIN(f->i->content.size - off, PAGE_SIZE);

		uintptr_t va = m->vm_start + off;
		if (!vpage_is_dirty(root, va)) {
			continue;
		}

		void *pa = vpage_get_pa_from_va(root, va, PTE_V | PTE_R);
		vpage_clear_dirty(root, va);
		res = inode_write(f->i, false, (void *)pa, off, n);
		if (res < 0) {
			goto err_unlock_inode;
		}

		written_back = true;
	}
	if (written_back) {
		inode_mark_dirty(f->i);
	}
	inode_unlock(f->i);

	return 0;

err_unlock_inode:
	inode_unlock(f->i);
	return res;
};

static void release_mmaps(struct mm *mm)
{
	clist_node_t *head = &mm->vmaps;
	clist_node_t *cur = head->next;
	while (cur != head) {
		clist_node_t *next = cur->next;
		struct vmap *m = vmap_from_node(cur);
		if (m->flags & MAP_SHARED) {
			int res = write_back(mm->root, m);
			if (res < 0) {
				printf("[WARN] mmap: write back failed (%d)\n",
				       res);
			}
		}
		// No need to unmap: vpage_tree_free() drops the whole tree.
		free_vmap(m);
		cur = next;
	}
}

struct mm *mm_alloc(void)
{
	struct mm *mm = ta_alloc(sizeof(*mm));
	if (!mm) {
		return NULL;
	}

	mm->root = page_alloc();
	if (!mm->root) {
		ta_free(mm);
		return NULL;
	}

	clist_init_node(&mm->vmaps);
	mm->brk = 0;
	atomic_init(&mm->users, 1);

	return mm;
}

struct mm *mm_get(struct mm *mm)
{
	// Atomic
	mm->users++;
	return mm;
}

void mm_put(struct mm *mm)
{
	// Atomic
	if (--mm->users > 0) {
		return;
	}

	if (!irq_is_enable_s()) {
		panic("This should be call with irq on.");
	}

	release_mmaps(mm);
	vpage_tree_free(mm->root);
	ta_free(mm);
}

int mmap_unmap(task_t *p, uintptr_t va, size_t len)
{
	if (va >= MMAP_BASE) {
		return -EINVAL;
	}
	if (len == 0) {
		return -EINVAL;
	}

	int res;
	clist_node_t *head = &p->mm->vmaps;
	clist_node_t *cur = head->next;

	uintptr_t unmap_start = ALIGN_DOWN(va, PAGE_SIZE);
	uintptr_t unmap_end = ALIGN_UP(va + len, PAGE_SIZE);

	while (cur != head) {
		clist_node_t *next = cur->next;
		struct vmap *m = vmap_from_node(cur);

		bool should_unmap = unmap_start <= m->vm_start &&
				    m->vm_end <= unmap_end;
		if (should_unmap) {
			if (m->flags & MAP_SHARED) {
				res = write_back(p->mm->root, m);
				if (res < 0) {
					return res;
				}
			}

			clist_remove(cur);
			for (uintptr_t addr = m->vm_start; addr < m->vm_end;
			     addr += PAGE_SIZE) {
				vpage_unmmap(p->mm->root, addr);
			}
			free_vmap(m);
		}

		cur = next;
	}

	return 0;
}

static int va_is_in_range(clist_node_t *node, void *args)
{
	uintptr_t va = *(uintptr_t *)args;
	struct vmap *m = vmap_from_node(node);

	return m->vm_start <= va && va <= m->vm_end;
}

static struct vmap *find_vmap(task_t *p, uintptr_t va)
{
	clist_node_t *node = clist_find(&p->mm->vmaps, va_is_in_range, &va);
	if (!node) {
		return NULL;
	}

	return vmap_from_node(node);
}

static uint16_t prot_to_pte_flags(struct vmap *m)
{
	uint16_t flags = 0;
	if (m->prot & PROT_READ) {
		flags |= PTE_R;
	}

	if (m->prot & PROT_WRITE) {
		if (m->flags & MAP_SHARED) {
			flags |= PTE_W;
		} else if (m->flags & MAP_PRIVATE) {
			flags |= PTE_COW;
		}
	}

	return flags;
}

int mmap_resolve_va(task_t *p, uintptr_t va)
{
	int res;
	struct vmap *m = find_vmap(p, va);
	if (!m) {
		return -ENOENT;
	}

	if (!m->f) {
		return -EINVAL;
	}

	void *buf = page_alloc();
	if (!buf) {
		return -ENOMEM;
	}

	size_t off = ALIGN_DOWN(va - m->vm_start, PAGE_SIZE);

	/* We use inode primitive to ensure we don't modify
	the file offset of the user file descriptor. */
	struct inode *i = m->f->i;
	res = inode_lock(i);
	if (res < 0) {
		goto err_put_page;
	}

	uint32_t isize = i->content.size;
	if (off >= isize) {
		res = -EFAULT;
		goto err_release_inode;
	}

	/* Last page may be partial: its tail stays zeroed by page_alloc(). */
	ssize_t readed = inode_read(i, false, buf, off,
				    min_t(size_t, isize - off, PAGE_SIZE));
	if (readed < 0) {
		res = readed;
		goto err_release_inode;
	}
	inode_unlock(i);

	res = vpage_map_user(p->mm->root, ALIGN_DOWN(va, PAGE_SIZE), buf,
			     prot_to_pte_flags(m));
	if (res < 0) {
		goto err_put_page;
	}

	return 0;

err_release_inode:
	inode_unlock(i);
err_put_page:
	page_put(buf);
	return res;
}

intptr_t mmap_file(task_t *p, [[maybe_unused]] uintptr_t va, struct file *f,
		   size_t len, uint16_t prot, uint16_t flags)
{
	if (len == 0) {
		return -EINVAL;
	}

	if (!(flags & MAP_PRIVATE) == !(flags & MAP_SHARED)) {
		return -EINVAL;
	}

	if (f == NULL) {
		return -EBADF;
	}

	if (!(f->mode & O_R)) {
		return -EACCES;
	}

	if (flags & MAP_SHARED && !(f->mode & O_W) && (prot & PROT_WRITE)) {
		return -EACCES;
	}

	clist_node_t *head = &p->mm->vmaps;
	clist_node_t *insert_after = head;
	uintptr_t gap_end = MMAP_BASE;

	for (clist_node_t *cur = head->next; cur != head; cur = cur->next) {
		struct vmap *m = vmap_from_node(cur);
		if (gap_end - m->vm_end >= len) {
			break;
		}

		gap_end = m->vm_start;
		insert_after = cur;
	}

	uintptr_t vm_end = ALIGN_DOWN(gap_end, PAGE_SIZE);
	size_t aligned_len = ALIGN_UP(len, PAGE_SIZE);
	uintptr_t vm_start = vm_end - aligned_len;

	if (vm_start < p->mm->brk) {
		return -EMFILE;
	}
	struct vmap *m = ta_alloc(sizeof(*m));
	if (!m) {
		return -ENOMEM;
	}
	m->vm_start = vm_start;
	m->vm_end = vm_end;
	m->prot = prot;
	m->flags = flags;
	m->f = file_get(f);
	clist_insert_after(insert_after, &m->node);

	return m->vm_start;
}

int iommap_kernel(void *addr, size_t size)
{
	int res;

	res = vpage_map_range(kroot, (uintptr_t)addr, addr, size,
			      PTE_R | PTE_W);
	if (res < 0) {
		return res;
	}

	mmu_flush_tlb();
	return 0;
}

int mmap_kernel(void)
{
	int res;
	kroot = page_alloc();
	if (!kroot) {
		return -ENOMEM;
	}

	res = vpage_map_range(kroot, (uintptr_t)_ram_start, (void *)_ram_start,
			      _ram_end - _ram_start, PTE_R | PTE_W | PTE_X);
	if (res < 0) {
		goto err_free_root;
	}

	res = vpage_map_range(kroot, TRAMPOLINE, _trampoline_start,
			      _trampoline_end - _trampoline_start,
			      PTE_X | PTE_R);
	if (res < 0) {
		goto err_free_root;
	}

	res = vpage_map(kroot, UART_BASE, (void *)UART_BASE, PTE_R | PTE_W);
	if (res < 0) {
		goto err_free_root;
	}

	res = vpage_map_range(kroot, PCI_ECAM_BASE_ADDRESS,
			      (void *)PCI_ECAM_BASE_ADDRESS, PCI_ECAM_SIZE,
			      PTE_R | PTE_W);
	if (res < 0) {
		goto err_free_root;
	}

	res = vpage_map_range(kroot, PLIC_MMIO_BASE, (void *)PLIC_MMIO_BASE,
			      PLIC_MMIO_SIZE, PTE_R | PTE_W);
	if (res < 0) {
		goto err_free_root;
	}

	mmap_switch_to_kernel();

	return 0;

err_free_root:
	page_put(kroot);
	return res;
}

int mmap_uspace(task_t *p)
{
	int res;
	if (!p->user) {
		res = -EINVAL;
		goto out;
	}

	struct mm *mm = mm_alloc();
	if (!mm) {
		res = -ENOMEM;
		goto out;
	}
	pte_t *root = mm->root;

	tframe_t *t = page_alloc();
	if (!t) {
		res = -ENOMEM;
		goto err_free_tree;
	}

	res = vpage_map(root, TRAPFRAME, t, PTE_R | PTE_W);
	if (res < 0) {
		// t failed to get mapped so no in the tree yet
		goto err_free_tframe;
	}

	p->mm = mm;
	p->tframe_pa = t;
	p->tframe_va = TRAPFRAME;

	return 0;

err_free_tframe:
	page_put(t);
err_free_tree:
	mm_put(mm);
out:
	return res;
}

int mmap_uimage(task_t *p)
{
	int res;

	res = mmap_uspace(p);
	if (res < 0) {
		return res;
	}

	pte_t *root = p->mm->root;

	// This is not U page because we will go back to user page
	// in S mode before the sret returns to user mode.
	res = vpage_map_range(root, TRAMPOLINE, _trampoline_start,
			      _trampoline_end - _trampoline_start,
			      PTE_X | PTE_R | PTE_G);
	if (res < 0) {
		goto err_free_tree;
	}

	// Stack lives within the image so it's allocated
	// during a image mapping.
	void *stack = page_alloc();
	if (!stack) {
		res = -ENOMEM;
		goto err_free_tree;
	}

	res = vpage_map_user(root, USTACK_TOP, stack, PTE_R | PTE_W);
	if (res < 0) {
		page_put(stack);
		goto err_free_tree;
	}

	return 0;

err_free_tree:
	mm_put(p->mm);
	p->mm = NULL;
	p->tframe_pa = NULL;
	return res;
}
