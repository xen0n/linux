/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_DMI_H
#define __ASM_DMI_H

#if defined(CONFIG_MACH_LOONGSON64)
#include <asm/mach-loongson64/dmi.h>
#else
#error "no DMI support wired up for the current board type"
#endif

#endif
