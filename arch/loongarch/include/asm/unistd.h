/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <uapi/asm/unistd.h>

#ifndef __NR_loongarch_ow_newfstatat
#define __NR_loongarch_ow_newfstatat 79
#endif
__SYSCALL(__NR_loongarch_ow_newfstatat, sys_loongarch_ow_newfstatat)
#ifndef __NR_loongarch_ow_fstat
#define __NR_loongarch_ow_fstat 80
#endif
__SYSCALL(__NR_loongarch_ow_fstat, sys_loongarch_ow_fstat)
#if 0
#ifndef __NR_loongarch_ow_getrlimit
#define __NR_loongarch_ow_getrlimit 163
#endif
__SYSCALL(__NR_loongarch_ow_getrlimit, sys_loongarch_ow_getrlimit)
#ifndef __NR_loongarch_ow_setrlimit
#define __NR_loongarch_ow_setrlimit 164
#endif
__SYSCALL(__NR_loongarch_ow_setrlimit, sys_loongarch_ow_setrlimit)
#endif

#define NR_syscalls (__NR_syscalls)
