/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <asm/regdef.h>
#include <linux/linkage.h>
	.text
	.cfi_sections   .debug_frame
/*
 * we have splited hw gcsr into three parts, so we can
 * calculate the code offset by gcsrid and jump here to
 * run the gcsrwr instruction.
 */
SYM_FUNC_START(set_hw_gcsr)
	addi.d      t0,   a0,   0
	addi.w      t1,   zero, 96
	bltu        t1,   t0,   1f
	la.pcrel    t0,   10f
	alsl.d      t0,   a0,   t0, 3
	jr          t0
1:
	addi.w      t1,   a0,   -128
	addi.w      t2,   zero, 15
	bltu        t2,   t1,   2f
	la.pcrel    t0,   11f
	alsl.d      t0,   t1,   t0, 3
	jr          t0
2:
	addi.w      t1,   a0,   -384
	addi.w      t2,   zero, 3
	bltu        t2,   t1,   3f
	la.pcrel    t0,   12f
	alsl.d      t0,   t1,   t0, 3
	jr          t0
3:
	addi.w      a0,   zero, -1
	jr          ra

/* range from 0x0(KVM_CSR_CRMD) to 0x60(KVM_CSR_LLBCTL) */
10:
	csrnum = 0
	.rept 0x61
	gcsrwr a1, csrnum
	jr ra
	csrnum = csrnum + 1
	.endr

/* range from 0x80(KVM_CSR_IMPCTL1) to 0x8f(KVM_CSR_TLBRPRMD) */
11:
	csrnum = 0x80
	.rept 0x10
	gcsrwr a1, csrnum
	jr ra
	csrnum = csrnum + 1
	.endr

/* range from 0x180(KVM_CSR_DMWIN0) to 0x183(KVM_CSR_DMWIN3) */
12:
	csrnum = 0x180
	.rept 0x4
	gcsrwr a1, csrnum
	jr ra
	csrnum = csrnum + 1
	.endr

SYM_FUNC_END(set_hw_gcsr)
