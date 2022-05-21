/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef _LOONGARCH_SETUP_H
#define _LOONGARCH_SETUP_H

#include <linux/types.h>
#include <uapi/asm/setup.h>

#define VECSIZE 0x200

extern unsigned long eentry;
extern unsigned long tlbrentry;
extern void cpu_cache_init(void);
extern void per_cpu_trap_init(int cpu);
extern void set_handler(unsigned long offset, void *addr, unsigned long len);
extern void set_merr_handler(unsigned long offset, void *addr, unsigned long len);

#ifdef CONFIG_EARLY_PRINTK
extern void prom_putchar(char);
extern void setup_early_printk(void);
#else
static inline void prom_putchar(char) {}
static inline void setup_early_printk(void) {}
#endif

#ifdef CONFIG_EARLY_PRINTK_8250
extern void setup_8250_early_printk_port(unsigned long base,
	unsigned int reg_shift, unsigned int timeout);
#else
static inline void setup_8250_early_printk_port(unsigned long base,
	unsigned int reg_shift, unsigned int timeout) {}
#endif

#endif /* __SETUP_H */
