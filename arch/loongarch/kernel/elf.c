// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/export.h>
#include <linux/sched.h>

#include <asm/cpu-features.h>
#include <asm/cpu-info.h>

int arch_elf_pt_proc(void *_ehdr, void *_phdr, struct file *elf,
		     bool is_interp, struct arch_elf_state *state)
{
	return 0;
}

int arch_check_elf(void *_ehdr, bool has_interpreter, void *_interp_ehdr,
		   struct arch_elf_state *state)
{
	struct elf64_hdr *ehdr = has_interpreter ? _interp_ehdr : _ehdr;
	if (state->abi_flavor == 0) {
		// no ABI tag section, have to resort to e_flags heuristics
		// assume every new-world binary is at least OBJ-v1
		state->abi_flavor = ((ehdr->e_flags & EF_LOONGARCH_OBJABI_MASK) != 0) ? 1 : 2;
		if (state->abi_flavor == 2)
			pr_info("%d (%s): e_flags=0x%x, old world\n",
				task_pid_nr(current), current->comm,
				ehdr->e_flags);
	}

	return 0;
}

void loongarch_set_personality_fcsr(struct arch_elf_state *state)
{
	current->thread.fpu.fcsr = boot_cpu_data.fpu_csr0;
}

void loongarch_set_personality_abi(struct arch_elf_state *state)
{
	if (state->abi_flavor == 2)
		set_thread_flag(TIF_OLD_WORLD);
}
