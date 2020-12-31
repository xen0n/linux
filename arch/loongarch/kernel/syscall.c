// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/unistd.h>
#include <linux/compiler.h>
#include <linux/sched/task_stack.h>

#include <asm/asm.h>
#include <asm/cacheflush.h>
#include <asm/asm-offsets.h>
#include <asm/signal.h>
#include <asm/shmparam.h>
#include <asm/switch_to.h>
#include <asm-generic/syscalls.h>

#undef __SYSCALL
#define __SYSCALL(nr, call)	[nr] = (call),

#define __str2(x) #x
#define __str(x) __str2(x)
#define save_static(symbol)                            \
__asm__(                                               \
       ".text\n\t"                                     \
       ".globl\t__" #symbol "\n\t"                     \
       ".align\t2\n\t"                                 \
       ".type\t__" #symbol ", @function\n\t"           \
	"__"#symbol":\n\t"                             \
       "st.d\t$r23,$r3,"__str(PT_R23)"\n\t"            \
       "st.d\t$r24,$r3,"__str(PT_R24)"\n\t"            \
       "st.d\t$r25,$r3,"__str(PT_R25)"\n\t"            \
       "st.d\t$r26,$r3,"__str(PT_R26)"\n\t"            \
       "st.d\t$r27,$r3,"__str(PT_R27)"\n\t"            \
       "st.d\t$r28,$r3,"__str(PT_R28)"\n\t"            \
       "st.d\t$r29,$r3,"__str(PT_R29)"\n\t"            \
       "st.d\t$r30,$r3,"__str(PT_R30)"\n\t"            \
       "st.d\t$r31,$r3,"__str(PT_R31)"\n\t"            \
       "b\t" #symbol "\n\t"                            \
       ".size\t__" #symbol",. - __" #symbol)

save_static(sys_clone);
save_static(sys_clone3);

asmlinkage void __sys_clone(void);
asmlinkage void __sys_clone3(void);

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags, unsigned long,
	fd, off_t, offset)
{
	if (offset & ~PAGE_MASK)
		return -EINVAL;
	return ksys_mmap_pgoff(addr, len, prot, flags, fd,
			       offset >> PAGE_SHIFT);
}

SYSCALL_DEFINE6(mmap2, unsigned long, addr, unsigned long, len,
	unsigned long, prot, unsigned long, flags, unsigned long, fd,
	unsigned long, pgoff)
{
	if (pgoff & (~PAGE_MASK >> 12))
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd,
			       pgoff >> (PAGE_SHIFT - 12));
}

#define sys_clone	__sys_clone
#define sys_clone3	__sys_clone3

void *sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = sys_ni_syscall,
#include <asm/unistd.h>
};
