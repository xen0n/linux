/* SPDX-License-Identifier: GPL-2.0 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/sched_clock.h>

#include <loongson.h>
#include <extcc.h>

static u64 notrace extcc_read(struct clocksource *cs)
{
	return read_extcc();
}

static u64 notrace extcc_sched_clock(void)
{
	return read_extcc();
}

static struct clocksource extcc_clocksource = {
	.name		= "extcc",
	.read		= extcc_read,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS | CLOCK_SOURCE_VALID_FOR_HRES,
	.archdata	= { .vdso_clock_mode = VDSO_CLOCK_EXTCC },
};

void __init extcc_clocksource_init(void)
{
	/* trust the firmware-provided frequency */
	u32 extcc_frequency = cpu_clock_freq;
	int ret;

	if (extcc_frequency == 0) {
		pr_err("Frequency not specified\n");
		return;
	}

	/*
	 * As for the rating, 200+ is good while 300+ is desirable.
	 * Use 1GHz as bar for "desirable"; most Loongson processors with support
	 * for ExtCC already fulfill this.
	 */
	extcc_clocksource.rating = 200 + extcc_frequency / 10000000;

	ret = clocksource_register_hz(&extcc_clocksource, extcc_frequency);
	if (ret < 0)
		pr_warn("Unable to register clocksource\n");

	/* mark extcc as sched clock */
	sched_clock_register(extcc_sched_clock, 64, extcc_frequency);
}

