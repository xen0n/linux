/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_LOONGSON64_EXTCC_H
#define __ASM_MACH_LOONGSON64_EXTCC_H

extern void extcc_clocksource_init(void);

static __always_inline u64 read_extcc(void)
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

#endif /* __ASM_MACH_LOONGSON64_EXTCC_H */
