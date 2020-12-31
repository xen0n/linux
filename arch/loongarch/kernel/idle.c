// SPDX-License-Identifier: GPL-2.0
/*
 * LoongArch idle loop support.
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/irqflags.h>
#include <asm/cpu.h>
#include <asm/idle.h>

void arch_cpu_idle(void)
{
	local_irq_enable();
	__arch_cpu_idle();
}
