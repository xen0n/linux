/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-offsets.h>
#include <asm/irqflags.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

	.text
	.cfi_sections	.debug_frame
	.align	5
SYM_FUNC_START(handle_sys)
	csrrd	t0, PERCPU_BASE_KS
	la.abs	t1, kernelsp
	add.d	t1, t1, t0
	or	t2, sp, zero
	ld.d	sp, t1, 0

	addi.d	sp, sp, -PT_SIZE
	cfi_st	t2, PT_R3
	cfi_rel_offset  sp, PT_R3
	csrrd	t2, LOONGARCH_CSR_PRMD
	st.d	t2, sp, PT_PRMD
	csrrd	t2, LOONGARCH_CSR_CRMD
	st.d	t2, sp, PT_CRMD
	csrrd	t2, LOONGARCH_CSR_ECFG
	st.d	t2, sp, PT_ECFG
	csrrd	t2, LOONGARCH_CSR_EUEN
	st.d	t2, sp, PT_EUEN
	cfi_st	ra, PT_R1
	cfi_st	a0, PT_R4
	cfi_st	a1, PT_R5
	cfi_st	a2, PT_R6
	cfi_st	a3, PT_R7
	cfi_st	a4, PT_R8
	cfi_st	a5, PT_R9
	cfi_st	a6, PT_R10
	cfi_st	a7, PT_R11
	csrrd	ra, LOONGARCH_CSR_EPC
	st.d	ra, sp, PT_EPC

	cfi_rel_offset ra, PT_EPC

	cfi_st	tp, PT_R2
	cfi_st	x0, PT_R21
	cfi_st	fp, PT_R22

	li.d	tp, ~_THREAD_MASK
	and	tp, tp, sp

	STI

	/* save the initial A0 value (needed in signal handlers) */
	st.d	a0, sp, PT_ORIG_A0
	ld.d	t1, sp, PT_EPC		# skip syscall on return
	addi.d	t1, t1, 4		# skip to next instruction
	st.d	t1, sp, PT_EPC

	li.d	t1, _TIF_WORK_SYSCALL_ENTRY
	LONG_L	t0, tp, TI_FLAGS	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, syscall_trace_entry

syscall_common:
	/* Check to make sure we don't jump to a bogus syscall number. */
	li.w	t0, __NR_syscalls
	sub.d	t2, a7, t0
	bgez	t2, illegal_syscall

	/* Syscall number held in a7 */
	slli.d	t0, a7, 3		# offset into table
	la	t2, sys_call_table
	add.d	t0, t2, t0
	ld.d	t2, t0, 0		#syscall routine
	beqz    t2, illegal_syscall

	jalr	t2			# Do The Real Thing (TM)

	ld.d	t1, sp, PT_R11		# syscall number
	addi.d	t1, t1, 1		# +1 for handle_signal
	st.d	t1, sp, PT_R0		# save it for syscall restarting
	st.d	v0, sp, PT_R4		# result

la64_syscall_exit:
	b	syscall_exit_partial

/* ------------------------------------------------------------------------ */

syscall_trace_entry:
	SAVE_STATIC
	move	a0, sp
	move	a1, a7
	bl	syscall_trace_enter

	blt	v0, zero, 1f			# seccomp failed? Skip syscall

	RESTORE_STATIC
	ld.d	a0, sp, PT_R4		# Restore argument registers
	ld.d	a1, sp, PT_R5
	ld.d	a2, sp, PT_R6
	ld.d	a3, sp, PT_R7
	ld.d	a4, sp, PT_R8
	ld.d	a5, sp, PT_R9
	ld.d	a6, sp, PT_R10
	ld.d	a7, sp, PT_R11		# Restore syscall (maybe modified)
	b	syscall_common

1:	b	syscall_exit

	/*
	 * The system call does not exist in this kernel
	 */

illegal_syscall:
	/* This also isn't a valid syscall, throw an error.  */
	li.w	v0, -ENOSYS			# error
	st.d	v0, sp, PT_R4
	b	la64_syscall_exit
SYM_FUNC_END(handle_sys)
