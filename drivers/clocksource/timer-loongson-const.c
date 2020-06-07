// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/sched_clock.h>

#include <loongson_regs.h>

static __always_inline u64 notrace __loongson_const_timer_read(void)
{
	u64 result;

	__asm__ __volatile__(
		".set	push\n\t"
		".set	arch=mips32r2\n\t"
		"rdhwr	%0, $30\n\t"
		".set	pop\n"
		: "=r"(result));

	return result;
}

static u64 notrace loongson_const_timer_read(struct clocksource *cs)
{
	return __loongson_const_timer_read();
}

static u64 notrace loongson_const_timer_sched_clock(void)
{
	return __loongson_const_timer_read();
}

static u32 loongson_const_timer_query_freq(void)
{
	u32 ccfreq = read_cpucfg(LOONGSON_CFG4);
	u32 cfg5 = read_cpucfg(LOONGSON_CFG5);
	u32 cfm = cfg5 & LOONGSON_CFG5_CFM;
	u32 cfd = (cfg5 & LOONGSON_CFG5_CFD) >> 16;
	return ccfreq * cfm / cfd;
}

static struct clocksource loongson_const_timer_clocksource = {
	.name			= "loongson-const",
	.read			= loongson_const_timer_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.flags			= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_VALID_FOR_HRES,
	.vdso_clock_mode	= VDSO_CLOCKMODE_LOONGSON_CONST,
};


static int __init loongson_const_timer_init_dt(struct device_node *n)
{
	u32 cfg2;
	u32 rev;
	u32 freq;
	int ret;

	/* The almost equivalent ExtCC timer present on pre-3A4000 cores has
	 * shortcomings, making it unsuitable to turn on unconditionally.
	 * So only continue if running on 3A4000+.
	 */
	if (!cpu_has_cfg())
		goto no_support;

	/* Check if global timer is present. */
	cfg2 = read_cpucfg(LOONGSON_CFG2);
	if (!(cfg2 & LOONGSON_CFG2_LGFTP))
		goto no_support;

	/* At least revision 2 is required. */
	rev = (cfg2 & LOONGSON_CFG2_LGFTPREV) >> 20;
	if (rev < 2)
		goto no_support;

	/* Query frequency from CPUCFG. */
	freq = loongson_const_timer_query_freq();
	if (unlikely(!freq)) {
		pr_err("Cannot probe constant timer frequency\n");
		return -EINVAL;
	}

	/*
	 * As for the rating, 200+ is good while 300+ is desirable.
	 * Use 1GHz as bar for "desirable".
	 */
	loongson_const_timer_clocksource.rating = 200 + freq / 10000000;

	ret = clocksource_register_hz(&loongson_const_timer_clocksource, freq);
	if (unlikely(ret)) {
		pr_err("Unable to register clocksource: %d\n", ret);
		return ret;
	}

	/* Mark as sched clock */
	sched_clock_register(loongson_const_timer_sched_clock, 64, freq);

	return 0;

no_support:
	pr_warn("CPU has no support for constant timer\n");
	return -ENXIO;
}

TIMER_OF_DECLARE(loongson_const_timer, "loongson,const-timer",
		 loongson_const_timer_init_dt);
