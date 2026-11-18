#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lib/err.h>
#include <lib/stdio.h>
#include <lib/tinyalloc.h>

#include <drivers/console.h>
#include <drivers/uart.h>
#include <drivers/virtio_blk.h>

#include <arch/asm_defs.h>
#include <arch/cpu.h>
#include <arch/plic.h>
#include <arch/trap.h>
#include <kernel/file.h>
#include <kernel/fs.h>
#include <kernel/memory.h>
#include <kernel/mmap.h>
#include <kernel/pages.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <user/kinterface.h>

/**
 * @brief User elf included via user_app.S.
 * FIX: drop me.
 */
extern char _user_elf_start[];

/** 
 * @brief Kernel entry point, called by all other hart than hart0 is cleared.
 * @warning This should not spawn any task. The task are spawned in init.
 * */
[[noreturn]] static void kernel_start(void)
{
	mmap_switch_to_kernel();
	int res = task_enter_idle();
	if (res < 0) {
		panic("[FAILURE] Init idle has failed");
	}

	irq_enable_s();
	time_enable_s_irq();
	plic_enable_s_external();
	/** Enable on all hart those irq */
	plic_enable_irq(VIRTIO_BLK_PLIC_ID);
	plic_enable_irq(UART_PLIC_ID);
	plic_set_treshold(0);

	task_idle();
}

/**
 * @brief Handle all the blocking initialization.
 */
static void init(void)
{
	int res;
	/* Blocking task that require blk io */
	res = fs_init();
	if (res < 0) {
		panic("[FAILURE] File system init has failed");
	}

	res = task_map_console(task_active());
	if (res < 0) {
		panic("[FAILURE] Console map has failed");
	}

	res = uart_spawn_daemons();
	if (res < 0) {
		panic("[FAILURE] Uart daemon spawn has failed");
	}

	res = time_spawn_daemons();
	if (res < 0) {
		panic("[FAILURE] time daemon spawn has failed");
	}

	struct file *user_elf = file_open("/user.elf", O_R);
	if (IS_ERR(user_elf)) {
		panic("[FAILURE] User Init elf image not mounted in the fs");
	};

	task_t *p = task_start_userland(user_elf, "User", NORMAL);
	if (IS_ERR(p)) {
		printf("[FAILURE]: failed to enter userland (%ld)\n",
		       PTR_ERR(p));
	}
	file_put(user_elf);
}

extern void enter_kernel(void (*entry)(void));
extern void delegate_traps(void);

bool boot_rdy = false;

/**
 * @brief Supervisor boot byt hart0.
 *
 * @warning This should only spawn the init task and no other task.
 */
[[noreturn]] static void sboot(void)
{
	int res;

	/* We start with uart to have immediat debug printf */
	uart_init();
	page_init();

	res = mmap_kernel();
	if (res < 0) {
		panic("[FAILURE] Kernel map has failed");
	}
	mmap_switch_to_kernel();

	res = console_init();
	if (res < 0) {
		if (res != -ENODEV) {
			panic("[FAILURE] Console init map has failed");
		}
	}

	res = virtio_blk_init();
	if (res < 0) {
		if (res != -ENODEV) {
			panic("[FAILURE] Virtio blk driver init has failed");
		}
	}
	ta_init(_kheap_start, _kheap_end, TA_HEAP_BLOCK, TA_BLOCK_SPLIT,
		TA_ALIGNMENT);

	//NOTE: for now we only have one runqueue, this might be
	// called per hart id when separate runqueus.
	scheduler_init();

	res = task_enter_idle();
	if (res < 0) {
		panic("[FAILURE] Init idle has failed");
	}

	irq_enable_s();
	time_enable_s_irq();
	plic_enable_s_external();
	plic_enable_irq(VIRTIO_BLK_PLIC_ID);
	plic_enable_irq(UART_PLIC_ID);
	plic_set_treshold(0);

	/* Once theire, the other hart are able to launch and will help to init the rest of the system */
	m2m_wmb();
	boot_rdy = true;

	/* This thread will make the rest of the init which can be blocked and will
     * fallback to the idle task when blocked */
	if (IS_ERR(task_kthread(init, "init", NORMAL))) {
		printf("[FAILURE]: failed to spawn init kthread \n");
	}
	task_idle();
}

void hart_init([[maybe_unused]] unsigned long hartid)
{
	pmp_allow_all();
	delegate_traps();
	trap_init();
	time_init();

	void (*entry)(void);
	if (hartid == BOOT_HART_ID) {
		entry = sboot;
	} else {
		entry = kernel_start;
	}
	enter_kernel(entry);
}
