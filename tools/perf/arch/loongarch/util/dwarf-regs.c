// SPDX-License-Identifier: GPL-2.0
/*
 * dwarf-regs.c : Mapping of DWARF debug register numbers into register names.
 *
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 2013 Cavium, Inc.
 */

#include <stdio.h>
#include <dwarf-regs.h>

static const char *loongarch_gpr_names[32] = {
	"$0", "$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9",
	"$10", "$11", "$12", "$13", "$14", "$15", "$16", "$17", "$18", "$19",
	"$20", "$21", "$22", "$23", "$24", "$25", "$26", "$27", "$28", "$29",
	"$30", "$31"
};

const char *get_arch_regstr(unsigned int n)
{
	n %= 32;
	return loongarch_gpr_names[n];
}
