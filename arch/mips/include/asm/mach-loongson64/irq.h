/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_LOONGSON64_IRQ_H_
#define __ASM_MACH_LOONGSON64_IRQ_H_

#include <boot_param.h>

#define NR_IRQS 512
/* cpu core interrupt numbers */
#define MIPS_CPU_IRQ_BASE 16

#include_next <irq.h>
#endif /* __ASM_MACH_LOONGSON64_IRQ_H_ */
