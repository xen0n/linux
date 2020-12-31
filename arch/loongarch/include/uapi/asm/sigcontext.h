/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _UAPI_ASM_SIGCONTEXT_H
#define _UAPI_ASM_SIGCONTEXT_H

#include <linux/types.h>
#include <linux/posix_types.h>
#include <asm/processor.h>

/* FP context was used */
#define USED_FP			(1 << 0)

struct sigcontext {
	__u64	sc_pc;
	__u64	sc_regs[32];
	__u32	sc_flags;
	__u32	sc_fcsr;
	__u32	sc_vcsr;
	__u64	sc_fcc;
	__u64	sc_scr[4];
	union fpureg sc_fpregs[32] FPU_ALIGN;
	__u8	sc_reserved[4096] __attribute__((__aligned__(16)));
};

#endif /* _UAPI_ASM_SIGCONTEXT_H */
