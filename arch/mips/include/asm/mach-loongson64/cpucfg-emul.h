/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LOONGSON_CPUCFG_EMUL_H_
#define _LOONGSON_CPUCFG_EMUL_H_

#include <asm/cpu-info.h>

#define LOONGSON_FPREV_MASK 0x7

#ifdef CONFIG_CPU_LOONGSON_CPUCFG_EMULATION

/* Finalize synthesis of CPUCFG data by patching the partially filled data
 * with dynamically detected CPU characteristics. This keeps the amount of
 * hard-coded logic at a minimum.
 */
void loongson_cpucfg_finish_synthesis(struct cpuinfo_mips *c);

static inline int read_cpucfg_val(struct cpuinfo_mips *c, __u64 sel)
{
	switch (sel) {
	case 0:
		return c->processor_id;
	case 1:
	case 2:
	case 3:
		return c->loongson_cpucfg_data[sel - 1];
	case 4:
	case 5:
		/* CPUCFG selects 4 and 5 are related to the processor clock.
		 * Unimplemented for now.
		 */
		return 0;
	case 6:
		/* CPUCFG select 6 is for the undocumented Safe Extension. */
		return 0;
	case 7:
		/* CPUCFG select 7 is for the virtualization extension.
		 * We don't know if the two currently known features are
		 * supported on older cores according to the public
		 * documentation, so leave this at zero.
		 */
		return 0;
	}

	/*
	 * Return 0 for unrecognized CPUCFG selects, which is real hardware
	 * behavior observed on Loongson 3A R4.
	 */
	return 0;
}
#else
static void loongson_cpucfg_finish_synthesis(struct cpuinfo_mips *c)
{
}

static inline int read_cpucfg_val(struct cpuinfo_mips *c, __u64 sel)
{
	return 0;
}
#endif

#endif /* _LOONGSON_CPUCFG_EMUL_H_ */
