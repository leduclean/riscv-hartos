#include <stddef.h>
#include <stdint.h>

#include <lib/clist.h>
#include <lib/container.h>
#include <lib/stdio.h>

#include <arch/cpu.h>
#include <arch/trap.h>
#include <kernel/mmap.h>
#include <kernel/scheduler.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/time.h>
#include <kernel/vpages.h>
#include <kernel/waitqueue.h>

extern void ctx_sw(ctx_t *old_ctx, ctx_t *new_ctx);

/** @brief Priority mapped ready queues. */
static clist_node_t rqs[PRIORITY_COUNT];

/**
 * @brief This lock protects all the queue management.
 */
static spinlock_t sched_lock = SPINLOCK_UNLOCKED;

/** @brief Highest ready priority. */
static priority highest_rdy = IDLE;

static void rm_from_wq_locked(task_t *task)
{

	if (clist_is_in_list(&task->sleep_node))
		clist_remove(&task->sleep_node);
	if (clist_is_in_list(&task->wait_node)) {
		clist_remove(&task->wait_node);
	}
}

void scheduler_rm_from_sched_queues(task_t *task)
{

	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	if (clist_is_in_list(&task->ready_node))
		clist_remove(&task->ready_node);
	rm_from_wq_locked(task);
	spinlock_unlock_irq_restore(&sched_lock, flags);
}

/**
 * @brief Check if a task should change the highest priority flag.
 *
 * @param task Task to check on.
 */
static void update_highest(task_t *task)
{
	priority prior_class = task->priority;
	if (priority_higher(prior_class, highest_rdy)) {
		highest_rdy = prior_class;
	}
}

/**
 * @brief Add a task at the end of its corresponding priority ready queue.
 *
 * @param task Task to enqueue.
 */
static void rq_enqueue_locked(task_t *task)
{
	update_highest(task);
	priority prior_class = task->priority;
	clist_node_t *rq = &rqs[prior_class];
	clist_push_back(rq, &task->ready_node);
}

static inline bool preempt_disabled(void)
{
	return this_cpu()->preempt_count > 0;
}

static inline void delay_schedule(void)
{
	this_cpu()->need_resched = true;
}

void scheduler_disable_preempt(void)
{
	this_cpu()->preempt_count++;
}

bool scheduler_preemptible(void)
{
	return this_cpu()->preempt_count == 0;
}

void scheduler_enable_preempt(void)
{
	struct cpu *hart = this_cpu();
	if (--hart->preempt_count == 0 && hart->need_resched) {
		hart->need_resched = false;
		scheduler_rotate();
	}
}

/** @brief Init all the ready queues and the priority flag. */
static void init_ready_queues(void)
{
	// Reset all the node of the ready queue
	for (int i = 0; i < PRIORITY_COUNT; i++) {
		clist_node_t *current_head = &rqs[i];
		clist_init_node(current_head);
	}
}

/**
 * @brief Pick the highest priority ready queue in the ready queues.
 *
 * @return Pointer to the highest priority queue (clist).
 */
static inline clist_node_t *highest_rq(void)
{
	return &rqs[highest_rdy];
}

/**
 * @brief Refresh the highest priority flag, scanning all the queues.
 *
 * @note The scan is done incrementing the index, since the highest priority
 * is 0.
 */
static void refresh_highest_prio_locked(void)
{
	// Test the supposate highest prio
	if (!clist_empty(&rqs[highest_rdy])) {
		return;
	}

	// scan the other queues (lower priority state are higher in priority index )
	for (int p = highest_rdy + 1; p < PRIORITY_COUNT; p++) {
		if (!clist_empty(&rqs[p])) {
			// Update the new highest prio
			highest_rdy = p;
			return;
		}
	}

	/* Found nothing */
	highest_rdy = IDLE;
}

/**
 * @brief Main rescheduling function, changing the context between tasks.
 *
 * @param next Task to switch on.
 */
static void do_ctx_switch(task_t *next)
{
	ctx_t *old_ctx = &task_active()->ctx;
	task_switch_active(next);
	ctx_sw(old_ctx, &next->ctx);
}

static inline clist_node_t *pick_next_locked(void)
{
	if (highest_rdy == IDLE) {
		return &this_cpu()->idle->ready_node;
	}

	clist_node_t *next_node = clist_pop_front(highest_rq());
	refresh_highest_prio_locked();
	return next_node;
}

static inline void put_prev_locked(void)
{
	task_t *current = task_active();
	if (current == this_cpu()->idle) {
		return;
	}

	rq_enqueue_locked(current);
}

static void switch_out_active_locked(void)
{
	clist_node_t *next_node = pick_next_locked();
	if (!next_node) {
		panic("Invariant broken: No idle task is available.");
	}

	do_ctx_switch(container_of(next_node, task_t, ready_node));
}

/**
 * @brief Switch out the active task on its inactivity.
 *
 * @note Called on a terminaison or on a sleep, it switches on the head of
 * the highest priority ready queue.
 */
static void switch_out_active(void)
{
	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	switch_out_active_locked();
	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_rotate(void)
{
	if (preempt_disabled()) {
		delay_schedule();
		return;
	}

	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	put_prev_locked();
	clist_node_t *next_node = pick_next_locked();
	if (next_node == &task_active()->ready_node) {
		/* Only this task is ready, no need to switch */
		spinlock_unlock_irq_restore(&sched_lock, flags);
		return;
	}

	do_ctx_switch(container_of(next_node, task_t, ready_node));
	spinlock_unlock_irq_restore(&sched_lock, flags);
}

/**
 * @brief Check if a priority preemption is needed and possible and does it if needed.
 *
 * @param task Task that may preempt the active one.
 */
static void check_and_preempt_locked(task_t *task)
{
	task_t *current = task_active();
	if (!current) {
		panic("Invariant broke, theire should always be an idle task");
		return;
	}

	if (priority_higher(task->priority, current->priority)) {
		delay_schedule();
	}
}

void scheduler_admit(task_t *task)
{
	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	rq_enqueue_locked(task);
	check_and_preempt_locked(task);
	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_finish_switch(void)
{
	//NOTE: We force this because there is no guarantee,
	// after a ktask spawn to have irq enable.
	// (ctx switch does not preserve the sstatus)
	spinlock_unlock(&sched_lock);
	irq_enable_s();
}

/**
 * @brief Sort policy of the sleeping queue.
 *
 * @param current Node already in the queue.
 * @param other Node being inserted.
 * @return 1 if @p current wakes up after @p other, 0 otherwise.
 */
static inline int wake_up_cmp(clist_node_t *current, clist_node_t *other)
{
	task_t *cur_task = container_of(current, task_t, sleep_node);
	task_t *other_task = container_of(other, task_t, sleep_node);
	return (cur_task->wake_up_time > other_task->wake_up_time);
}

/** @brief Tasks sleeping on a timer, sorted by wake up date. */
static clist_node_t sleep_q = CLIST_NODE_INITIALIZER(sleep_q);

/**
 * @brief Insert a task in the sleeping queue, sorted by wake up time.
 *
 * @param task Task to insert.
 */
static void insert_sleep_locked(task_t *task)
{
	clist_node_t *node = &task->sleep_node;
	clist_insert_sorted(&sleep_q, node, wake_up_cmp);
}

static void ready_task_locked(task_t *task)
{
	if (!((task->state == SLEEPING) || (task->state == BLOCKED))) {
		return;
	}
	task_wake(task);
	rm_from_wq_locked(task);
	rq_enqueue_locked(task);
	check_and_preempt_locked(task);
}

void scheduler_ready_task(task_t *task)
{
	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	ready_task_locked(task);
	spinlock_unlock_irq_restore(&sched_lock, flags);
}

/**
 * @brief Wake up policy applied on each item of the sleeping queue.
 *
 * @param node Sleeping node of the task.
 * @param arg Current time, in secondes.
 * @return -1 to break the for each iteration on the sorted list, 0 else.
 */
static inline int wake_up_sleeping_cb_locked(clist_node_t *node, void *arg)
{
	uint32_t now = *(uint32_t *)arg;
	task_t *task = container_of(node, task_t, sleep_node);
	if (task->wake_up_time > now)
		return 0;

	clist_remove(node);
	ready_task_locked(task);
	return 0;
}

/**
 * @brief Move a task from its ready queue to a wait queue.
 *
 * @param task Task to block.
 * @param wq Wait queue to block it on.
 */
static inline void move_to_wq_locked(task_t *task, wait_queue_t *wq)
{
	task_block(task);
	wq_enqueue(wq, &task->wait_node);
}

void scheduler_exit(int exit_code)
{
	task_t *current = task_active();
	task_exit_mm(current);
	task_zombify(current, exit_code);
	switch_out_active();
	panic("Zombie exit");
}

static inline int exit_tgid_cb(clist_node_t *node, void *arg)
{
	uint8_t tgid = *(uint8_t *)arg;

	task_t *current = task_active();
	task_t *p = container_of(node, task_t, task_node);

	//TODO: On multicore this is gonna crash.
	// maybe a flag for killed needs to be add.
	if (tgid == p->tgid && p != current) {
		// No zombie state, we directly destroy the thread
		task_destroy(p);
	}

	return 0;
}

void scheduler_exit_group(int exit_code)
{
	task_t *task = task_active();
	task_table_for_each(exit_tgid_cb, &task->tgid);
	scheduler_exit(exit_code);
}

void scheduler_sleep(uint32_t nbr_secs)
{
	task_t *task = task_active();
	task_sleep(task, nbr_secs);

	unsigned long flags = spinlock_lock_irq_save(&sched_lock);

	insert_sleep_locked(task);
	switch_out_active_locked();

	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_wake_sleeping(void)
{
	unsigned long flags = spinlock_lock_irq_save(&sched_lock);

	uint32_t now = time_seconds();
	clist_for_each(&sleep_q, wake_up_sleeping_cb_locked, &now);

	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_block_on(wait_queue_t *wq)
{
	task_t *task = task_active();

	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	move_to_wq_locked(task, wq);
	switch_out_active_locked();

	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_block_on_locked(wait_queue_t *wq, spinlock_t *s)
{
	task_t *task = task_active();

	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	move_to_wq_locked(task, wq);
	spinlock_unlock(s);
	switch_out_active_locked();
	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_block_on_with_timeout(wait_queue_t *wq, uint32_t timeout_secs)
{
	task_t *task = task_active();

	unsigned long flags = spinlock_lock_irq_save(&sched_lock);
	move_to_wq_locked(task, wq);
	if (timeout_secs > 0) {
		task_sleep(task, timeout_secs);
		insert_sleep_locked(task);
	}
	switch_out_active_locked();

	spinlock_unlock_irq_restore(&sched_lock, flags);
}

/**
 * @brief Wake up policy applied to a wait queue.
 *
 * @param current Node to compute on.
 * @param args Additional args, unused.
 * @return 0 (invariant).
 */
static inline int wake_up_waiting_cb(clist_node_t *current,
				     [[maybe_unused]] void *args)
{
	// Wake up the task from the waiting queue
	ready_task_locked(container_of(current, task_t, wait_node));
	return 0;
}

void scheduler_wake_waiting_queue(wait_queue_t *wq)
{
	unsigned long flags = spinlock_lock_irq_save(&sched_lock);

	wq_for_each_and_del(wq, wake_up_waiting_cb, NULL);

	spinlock_unlock_irq_restore(&sched_lock, flags);
}

void scheduler_init(void)
{
	init_ready_queues();
}
