// SPDX-License-Identifier: GPL-2.0

#include <linux/smp.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>

#include <loongson_regs.h>
#include <cpucfg-emul.h>

static u32 get_loongson_fprev(struct cpuinfo_mips *c)
{
	return (c->fpu_id & LOONGSON_FPREV_MASK) << LOONGSON_CFG1_FPREV_OFFSET;
}

static void patch_cpucfg_sel1(struct cpuinfo_mips *c)
{
	u64 ases = c->ases;
	u64 options = c->options;
	u32 data = c->loongson3_cpucfg_data[0];

	if (options & MIPS_CPU_FPU) {
		data |= LOONGSON_CFG1_FP;
		data |= get_loongson_fprev(c);
	}
	if (ases & MIPS_ASE_LOONGSON_MMI)
		data |= LOONGSON_CFG1_MMI;
	if (ases & MIPS_ASE_MSA)
		data |= LOONGSON_CFG1_MSA1;

	c->loongson3_cpucfg_data[0] = data;
}

static void patch_cpucfg_sel2(struct cpuinfo_mips *c)
{
	u64 ases = c->ases;
	u64 options = c->options;
	u32 data = c->loongson3_cpucfg_data[1];

	if (ases & MIPS_ASE_LOONGSON_EXT)
		data |= LOONGSON_CFG2_LEXT1;
	if (ases & MIPS_ASE_LOONGSON_EXT2)
		data |= LOONGSON_CFG2_LEXT2;
	if (options & MIPS_CPU_LDPTE)
		data |= LOONGSON_CFG2_LSPW;

	if (ases & MIPS_ASE_VZ)
		data |= LOONGSON_CFG2_LVZP;
	else
		data &= ~LOONGSON_CFG2_LVZREV;

	c->loongson3_cpucfg_data[1] = data;
}

static void patch_cpucfg_sel3(struct cpuinfo_mips *c)
{
	u64 ases = c->ases;
	u32 data = c->loongson3_cpucfg_data[2];

	if (ases & MIPS_ASE_LOONGSON_CAM) {
		data |= LOONGSON_CFG3_LCAMP;
	} else {
		data &= ~LOONGSON_CFG3_LCAMREV;
		data &= ~LOONGSON_CFG3_LCAMNUM;
		data &= ~LOONGSON_CFG3_LCAMKW;
		data &= ~LOONGSON_CFG3_LCAMVW;
	}

	c->loongson3_cpucfg_data[2] = data;
}

void loongson3_cpucfg_finish_synthesis(struct cpuinfo_mips *c)
{
	/* CPUs with CPUCFG support don't need to synthesize anything. */
	if (cpu_has_cfg())
		return;

	patch_cpucfg_sel1(c);
	patch_cpucfg_sel2(c);
	patch_cpucfg_sel3(c);
}
