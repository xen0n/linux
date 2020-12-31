/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/irqdomain.h>

#define IRQ_STACK_SIZE			THREAD_SIZE
#define IRQ_STACK_START			(IRQ_STACK_SIZE - 16)

DECLARE_PER_CPU(unsigned long, irq_stack);

/*
 * The highest address on the IRQ stack contains a dummy frame put down in
 * genex.S (except_vec_vi_handler) which is structured as follows:
 *
 *   top ------------
 *       | task sp  | <- irq_stack[cpu] + IRQ_STACK_START
 *       ------------
 *       |          | <- First frame of IRQ context
 *       ------------
 *
 * task sp holds a copy of the task stack pointer where the struct pt_regs
 * from exception entry can be found.
 */

static inline bool on_irq_stack(int cpu, unsigned long sp)
{
	unsigned long low = per_cpu(irq_stack, cpu);
	unsigned long high = low + IRQ_STACK_SIZE;

	return (low <= sp && sp <= high);
}

struct irq_data;
struct device_node;

void arch_init_irq(void);
void do_IRQ(unsigned int irq);
void spurious_interrupt(void);
int loongarch_cpu_irq_init(struct device_node *of_node, struct device_node *parent);

#define NR_IRQS_LEGACY 16

void arch_trigger_cpumask_backtrace(const struct cpumask *mask,
					bool exclude_self);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace

#define NR_IRQS	(64 + 256)

#define LOONGSON_CPU_UART0_VEC		10 /* CPU UART0 */
#define LOONGSON_CPU_THSENS_VEC		14 /* CPU Thsens */
#define LOONGSON_CPU_HT0_VEC		16 /* CPU HT0 irq vector base number */
#define LOONGSON_CPU_HT1_VEC		24 /* CPU HT1 irq vector base number */

/* IRQ number definitions */
#define LOONGSON_LPC_IRQ_BASE		0
#define LOONGSON_LPC_LAST_IRQ		(LOONGSON_LPC_IRQ_BASE + 15)

#define LOONGSON_CPU_IRQ_BASE		16
#define LOONGSON_LINTC_IRQ		(LOONGSON_CPU_IRQ_BASE + 2) /* IP2 for CPU local interrupt controller */
#define LOONGSON_BRIDGE_IRQ		(LOONGSON_CPU_IRQ_BASE + 3) /* IP3 for bridge */
#define LOONGSON_TIMER_IRQ		(LOONGSON_CPU_IRQ_BASE + 11) /* IP11 CPU Timer */
#define LOONGSON_CPU_LAST_IRQ		(LOONGSON_CPU_IRQ_BASE + 14)

#define LOONGSON_PCH_IRQ_BASE		64
#define LOONGSON_PCH_ACPI_IRQ		(LOONGSON_PCH_IRQ_BASE + 47)
#define LOONGSON_PCH_LAST_IRQ		(LOONGSON_PCH_IRQ_BASE + 64 - 1)

#define LOONGSON_MSI_IRQ_BASE		(LOONGSON_PCH_IRQ_BASE + 64)
#define LOONGSON_MSI_LAST_IRQ		(LOONGSON_PCH_IRQ_BASE + 256 - 1)

#define GSI_MIN_CPU_IRQ		LOONGSON_CPU_IRQ_BASE
#define GSI_MAX_CPU_IRQ		(LOONGSON_CPU_IRQ_BASE + 48 - 1)
#define GSI_MIN_PCH_IRQ		LOONGSON_PCH_IRQ_BASE
#define GSI_MAX_PCH_IRQ		(LOONGSON_PCH_IRQ_BASE + 256 - 1)

#define MAX_PCH_PICS 4

extern int find_pch_pic(u32 gsi);

static inline void eiointc_enable(void)
{
	uint64_t misc;

	misc = iocsr_readq(LOONGARCH_IOCSR_MISC_FUNC);
	misc |= IOCSR_MISC_FUNC_EXT_IOI_EN;
	iocsr_writeq(misc, LOONGARCH_IOCSR_MISC_FUNC);
}

struct acpi_madt_lio_pic;
struct acpi_madt_eio_pic;
struct acpi_madt_ht_pic;
struct acpi_madt_bio_pic;
struct acpi_madt_msi_pic;
struct acpi_madt_lpc_pic;

struct fwnode_handle *liointc_acpi_init(struct acpi_madt_lio_pic *acpi_liointc);
struct fwnode_handle *eiointc_acpi_init(struct acpi_madt_eio_pic *acpi_eiointc);

struct fwnode_handle *htvec_acpi_init(struct fwnode_handle *parent,
					struct acpi_madt_ht_pic *acpi_htvec);
struct fwnode_handle *pch_lpc_acpi_init(struct fwnode_handle *parent,
					struct acpi_madt_lpc_pic *acpi_pchlpc);
struct fwnode_handle *pch_msi_acpi_init(struct fwnode_handle *parent,
					struct acpi_madt_msi_pic *acpi_pchmsi);
struct fwnode_handle *pch_pic_acpi_init(struct fwnode_handle *parent,
					struct acpi_madt_bio_pic *acpi_pchpic);

extern struct acpi_madt_lio_pic *acpi_liointc;
extern struct acpi_madt_eio_pic *acpi_eiointc;
extern struct acpi_madt_ht_pic *acpi_htintc;
extern struct acpi_madt_lpc_pic *acpi_pchlpc;
extern struct acpi_madt_msi_pic *acpi_pchmsi;
extern struct acpi_madt_bio_pic *acpi_pchpic[MAX_PCH_PICS];

extern struct fwnode_handle *acpi_liointc_handle;
extern struct fwnode_handle *acpi_msidomain_handle;
extern struct fwnode_handle *acpi_picdomain_handle[MAX_PCH_PICS];

extern void fixup_irqs(void);
extern void loongson3_ipi_interrupt(int irq);

#include <asm-generic/irq.h>

#endif /* _ASM_IRQ_H */
