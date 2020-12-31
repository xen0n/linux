/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/init.h>

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/cacheops.h>
#include <asm/fpregdef.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

	.align	5	/* 32 byte rollback region */
SYM_FUNC_START(__arch_cpu_idle)
	/* start of rollback region */
	LONG_L	t0, tp, TI_FLAGS
	nop
	andi	t0, t0, _TIF_NEED_RESCHED
	bnez	t0, 1f
	nop
	nop
	nop
	idle	0
	/* end of rollback region */
1:
	jirl	zero, ra, 0
SYM_FUNC_END(__arch_cpu_idle)

/*
 * Common Vectored Interrupt
 * Complete the register saves and invoke the do_vint() handler
 */
SYM_FUNC_START(handle_vint)
	la	t1, __arch_cpu_idle
	LONG_L  t0, sp, PT_EPC
	/* 32 byte rollback region */
	ori	t0, t0, 0x1f
	xori	t0, t0, 0x1f
	bne	t0, t1, 1f
	LONG_S  t0, sp, PT_EPC
1:	SAVE_TEMP
	SAVE_STATIC
	KMODE

	move	a1, sp
	bl	do_vint

	RESTORE_ALL_AND_RET
SYM_FUNC_END(handle_vint)

	.macro	BUILD_VI_HANDLER num
	.align	5
SYM_FUNC_START(handle_vi_\num)
	BACKUP_T0T1
	SAVE_SOME
	addi.d	a0, zero, \num
	la.abs	t0, handle_vint
	jirl	zero, t0, 0
SYM_FUNC_END(handle_vi_\num)
	.endm

	BUILD_VI_HANDLER 0
	BUILD_VI_HANDLER 1
	BUILD_VI_HANDLER 2
	BUILD_VI_HANDLER 3
	BUILD_VI_HANDLER 4
	BUILD_VI_HANDLER 5
	BUILD_VI_HANDLER 6
	BUILD_VI_HANDLER 7
	BUILD_VI_HANDLER 8
	BUILD_VI_HANDLER 9
	BUILD_VI_HANDLER 10
	BUILD_VI_HANDLER 11
	BUILD_VI_HANDLER 12
	BUILD_VI_HANDLER 13

	.align	3
SYM_DATA_START(vi_table)
	PTR	handle_vi_0
	PTR	handle_vi_1
	PTR	handle_vi_2
	PTR	handle_vi_3
	PTR	handle_vi_4
	PTR	handle_vi_5
	PTR	handle_vi_6
	PTR	handle_vi_7
	PTR	handle_vi_8
	PTR	handle_vi_9
	PTR	handle_vi_10
	PTR	handle_vi_11
	PTR	handle_vi_12
	PTR	handle_vi_13
SYM_DATA_END(vi_table)

SYM_FUNC_START(except_vec_cex)
	b	cache_parity_error
	nop
SYM_FUNC_END(except_vec_cex)

	.macro	__build_prep_badv
	csrrd	t0, LOONGARCH_CSR_BADV
	PTR_S	t0, sp, PT_BVADDR
	KMODE
	.endm

	.macro	__build_prep_fcsr
	movfcsr2gr	a1, fcsr0
	KMODE
	.endm

	.macro	__build_prep_none
	KMODE
	.endm

	.macro	__BUILD_silent exception
	.endm

	.macro	__BUILD_verbose nexception
	LONG_L	a1, sp, PT_EPC
	ASM_PRINT("Got \nexception at %016lx\012")
	.endm

	.macro	BUILD_HANDLER exception handler prep verbose
	.align	5
	SYM_FUNC_START(handle_\exception)
	BACKUP_T0T1
	SAVE_ALL

	__build_prep_\prep
	__BUILD_\verbose \exception
	move	a0, sp
	la.abs	t0, do_\handler
	jirl    ra, t0, 0

	RESTORE_ALL_AND_RET
	SYM_FUNC_END(handle_\exception)
	.endm

	BUILD_HANDLER ade ade badv silent
	BUILD_HANDLER ale ale badv silent
	BUILD_HANDLER bp bp none silent
	BUILD_HANDLER fpe fpe fcsr silent
	BUILD_HANDLER fpu fpu none silent
	BUILD_HANDLER lsx lsx none silent
	BUILD_HANDLER lasx lasx none silent
	BUILD_HANDLER lbt lbt none silent
	BUILD_HANDLER ri ri none silent
	BUILD_HANDLER watch watch none silent
	BUILD_HANDLER reserved reserved none verbose	/* others */

SYM_FUNC_START(handle_sys)
	la.abs	t0, handle_syscall
	jirl    zero, t0, 0
SYM_FUNC_END(handle_sys)
