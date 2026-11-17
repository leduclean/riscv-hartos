/**
 * @file
 * @brief Plic configuration and dispatch.
 */

#pragma once

#include <stdint.h>

#include <arch/csr.h>

/**
 * @brief Enable a specific external device irq.
 *
 * @param irq_id The PLIC id of the peripheric.
 * @param priority The wanted priority of the irq.
 */
void plic_enable_irq(uint32_t irq_id, uint32_t priority);

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
