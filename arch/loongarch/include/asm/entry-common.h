/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARCH_LOONGARCH_ENTRY_COMMON_H
#define ARCH_LOONGARCH_ENTRY_COMMON_H

#include <linux/sched.h>
#include <linux/processor.h>

#include <asm/syscall.h>

static inline bool on_thread_stack(void)
{
	return !(((unsigned long)(current->stack) ^ current_stack_pointer) & ~(THREAD_SIZE - 1));
}

static inline __must_check int arch_syscall_enter_tracehook(struct pt_regs *regs)
{
	int ret = tracehook_report_syscall_entry(regs);

	if (ret)
		syscall_set_return_value(current, current_pt_regs(), -ENOSYS, 0);

	return ret;
}

#define arch_syscall_enter_tracehook arch_syscall_enter_tracehook

#endif
