/* TODO: license */

#ifndef __ASM_MACH_LOONGSON64_LOONGSON_EXTCC_H
#define __ASM_MACH_LOONGSON64_LOONGSON_EXTCC_H

#include <linux/types.h>

/**
 * rdtsc() - returns the current TSC without ordering constraints
 *
 * rdtsc() returns the result of RDTSC as a 64-bit integer.  The
 * only ordering constraint it supplies is the ordering implied by
 * "asm volatile": it will put the RDTSC in the place you expect.  The
 * CPU can and will speculatively execute that RDTSC, though, so the
 * results can be non-monotonic if compared on different CPUs.
 */
static __always_inline cycle_t rdextcc(void)
{
	cycle_t result;

	__asm__ __volatile__(
		".set	push\n\t"
		".set	arch=loongson3a\n\t"
		".set	noreorder\n\t"
		"rdhwr	%0, $30\n\t"
		".set	pop\n"
		: "=r"(result));

	return result;
}

/**
 * rdtsc_ordered() - read the current TSC in program order
 *
 * rdtsc_ordered() returns the result of RDTSC as a 64-bit integer.
 * It is ordered like a load to a global in-memory counter.  It should
 * be impossible to observe non-monotonic rdtsc_unordered() behavior
 * across multiple CPUs as long as the TSC is synced.
 */
static __always_inline cycle_t rdextcc_ordered(void)
{
	cycle_t result;

	__asm__ __volatile__(
		".set	push\n\t"
		".set	arch=loongson3a\n\t"
		".set	noreorder\n\t"
		"sync\n\t"
		"rdhwr	%0, $30\n\t"
		".set	pop\n"
		: "=r"(result));

	return result;
}

#endif /* __ASM_MACH_LOONGSON64_LOONGSON_EXTCC_H */
