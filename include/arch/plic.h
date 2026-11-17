/**
 * @file
 * @brief Plic configuration and dispatch.
 */

#pragma once

#include <stdint.h>

#include <arch/csr.h>

/**
 * @brief Enable a specific external device irq, at hart level.
 *
 * @param irq_id The PLIC id of the peripheric.
 */
void plic_enable_irq(uint32_t irq_id);

/**
 * @brief Set the global (not pre hart) pty of this irq.
 *
 * @note The range of priority is between 0 and 7, 0 meaning never interrupt.
 * A higher value is clamped to 7.
 *
 * @param irq_id Plic source id of the device.
 * @param priority Priority to set.
 */
void plic_set_pty(uint32_t irq_id, uint32_t priority);

/**
 * @brief Set the priority threshold of the plic target.
 *
 * @note Supports 7 levels of priority: a threshold value of zero permits all
 * interrupts with a non-zero priority, whereas a value of 7 masks all
 * interrupts. A higher value is clamped to 7.
 *
 * @param threshold Threshold to set.
 */
void plic_set_treshold(uint32_t threshold);

static inline void plic_enable_s_external(void)
{
	csr_set(sie, SIE_SEIE);
}

/** @brief Claim the external irq, dispatch it to its device and complete it. */
void plic_handle_irq(void);
