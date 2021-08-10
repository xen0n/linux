// SPDX-License-Identifier: GPL-2.0
/*
 * Handle unaligned accesses by emulation.
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1996, 1998, 1999, 2002 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */
#include <linux/context_tracking.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/perf_event.h>

asmlinkage void do_ade(struct pt_regs *regs)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();

	die_if_kernel("Kernel ade access", regs);
	force_sig(SIGBUS);

	/*
	 * On return from the signal handler we should advance the epc
	 */
	exception_exit(prev_state);
}

asmlinkage void do_ale(struct pt_regs *regs)
{
	enum ctx_state prev_state;

	prev_state = exception_enter();
	perf_sw_event(PERF_COUNT_SW_ALIGNMENT_FAULTS,
			1, regs, regs->csr_badvaddr);

	/*
	 * Currently we only support systems with hardware unaligned accesses
	 * enabled, so die unconditionally on getting this exception.
	 */

	die_if_kernel("Kernel unaligned instruction access", regs);
	force_sig(SIGBUS);

	/*
	 * On return from the signal handler we should advance the epc
	 */
	exception_exit(prev_state);
}
