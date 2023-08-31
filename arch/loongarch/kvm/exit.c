// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/vmalloc.h>
#include <asm/fpu.h>
#include <asm/inst.h>
#include <asm/time.h>
#include <asm/tlb.h>
#include <asm/loongarch.h>
#include <asm/numa.h>
#include <asm/kvm_vcpu.h>
#include <asm/kvm_csr.h>
#include <linux/kvm_host.h>
#include <asm/mmzone.h>
#include "trace.h"

static unsigned long _kvm_emu_read_csr(struct kvm_vcpu *vcpu, int csrid)
{
	struct loongarch_csrs *csr = vcpu->arch.csr;
	unsigned long val = 0;

	if (get_gcsr_flag(csrid) & SW_GCSR)
		val = kvm_read_sw_gcsr(csr, csrid);
	else
		pr_warn_once("Unsupport csrread 0x%x with pc %lx\n",
			csrid, vcpu->arch.pc);
	return val;
}

static void _kvm_emu_write_csr(struct kvm_vcpu *vcpu, int csrid,
	unsigned long val)
{
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(csrid) & SW_GCSR)
		kvm_write_sw_gcsr(csr, csrid, val);
	else
		pr_warn_once("Unsupport csrwrite 0x%x with pc %lx\n",
				csrid, vcpu->arch.pc);
}

static void _kvm_emu_xchg_csr(struct kvm_vcpu *vcpu, int csrid,
	unsigned long csr_mask, unsigned long val)
{
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(csrid) & SW_GCSR) {
		unsigned long orig;

		orig = kvm_read_sw_gcsr(csr, csrid);
		orig &= ~csr_mask;
		orig |= val & csr_mask;
		kvm_write_sw_gcsr(csr, csrid, orig);
	} else
		pr_warn_once("Unsupport csrxchg 0x%x with pc %lx\n",
				csrid, vcpu->arch.pc);
}

static int _kvm_handle_csr(struct kvm_vcpu *vcpu, larch_inst inst)
{
	unsigned int rd, rj, csrid;
	unsigned long csr_mask;
	unsigned long val = 0;

	/*
	 * CSR value mask imm
	 * rj = 0 means csrrd
	 * rj = 1 means csrwr
	 * rj != 0,1 means csrxchg
	 */
	rd = inst.reg2csr_format.rd;
	rj = inst.reg2csr_format.rj;
	csrid = inst.reg2csr_format.csr;

	/* Process CSR ops */
	if (rj == 0) {
		/* process csrrd */
		val = _kvm_emu_read_csr(vcpu, csrid);
		vcpu->arch.gprs[rd] = val;
	} else if (rj == 1) {
		/* process csrwr */
		val = vcpu->arch.gprs[rd];
		_kvm_emu_write_csr(vcpu, csrid, val);
	} else {
		/* process csrxchg */
		val = vcpu->arch.gprs[rd];
		csr_mask = vcpu->arch.gprs[rj];
		_kvm_emu_xchg_csr(vcpu, csrid, csr_mask, val);
	}

	return EMULATE_DONE;
}

int _kvm_emu_iocsr(larch_inst inst, struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	u32 rd, rj, opcode;
	u32 addr;
	unsigned long val;
	int ret;

	/*
	 * Each IOCSR with different opcode
	 */
	rd = inst.reg2_format.rd;
	rj = inst.reg2_format.rj;
	opcode = inst.reg2_format.opcode;
	addr = vcpu->arch.gprs[rj];
	ret = EMULATE_DO_IOCSR;
	run->iocsr_io.phys_addr = addr;
	run->iocsr_io.is_write = 0;

	/* LoongArch is Little endian */
	switch (opcode) {
	case iocsrrdb_op:
		run->iocsr_io.len = 1;
		break;
	case iocsrrdh_op:
		run->iocsr_io.len = 2;
		break;
	case iocsrrdw_op:
		run->iocsr_io.len = 4;
		break;
	case iocsrrdd_op:
		run->iocsr_io.len = 8;
		break;
	case iocsrwrb_op:
		run->iocsr_io.len = 1;
		run->iocsr_io.is_write = 1;
		break;
	case iocsrwrh_op:
		run->iocsr_io.len = 2;
		run->iocsr_io.is_write = 1;
		break;
	case iocsrwrw_op:
		run->iocsr_io.len = 4;
		run->iocsr_io.is_write = 1;
		break;
	case iocsrwrd_op:
		run->iocsr_io.len = 8;
		run->iocsr_io.is_write = 1;
		break;
	default:
		ret = EMULATE_FAIL;
		break;
	}

	if (ret == EMULATE_DO_IOCSR) {
		if (run->iocsr_io.is_write) {
			val = vcpu->arch.gprs[rd];
			memcpy(run->iocsr_io.data, &val, run->iocsr_io.len);
		}
		vcpu->arch.io_gpr = rd;
	}

	return ret;
}

int _kvm_complete_iocsr_read(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	unsigned long *gpr = &vcpu->arch.gprs[vcpu->arch.io_gpr];
	enum emulation_result er = EMULATE_DONE;

	switch (run->iocsr_io.len) {
	case 8:
		*gpr = *(s64 *)run->iocsr_io.data;
		break;
	case 4:
		*gpr = *(int *)run->iocsr_io.data;
		break;
	case 2:
		*gpr = *(short *)run->iocsr_io.data;
		break;
	case 1:
		*gpr = *(char *) run->iocsr_io.data;
		break;
	default:
		kvm_err("Bad IOCSR length: %d,addr is 0x%lx",
				run->iocsr_io.len, vcpu->arch.badv);
		er = EMULATE_FAIL;
		break;
	}

	return er;
}

int _kvm_emu_idle(struct kvm_vcpu *vcpu)
{
	++vcpu->stat.idle_exits;
	trace_kvm_exit_idle(vcpu, KVM_TRACE_EXIT_IDLE);

	if (!kvm_arch_vcpu_runnable(vcpu)) {
		/*
		 * Switch to the software timer before halt-polling/blocking as
		 * the guest's timer may be a break event for the vCPU, and the
		 * hypervisor timer runs only when the CPU is in guest mode.
		 * Switch before halt-polling so that KVM recognizes an expired
		 * timer before blocking.
		 */
		kvm_save_timer(vcpu);
		kvm_vcpu_block(vcpu);
	}

	return EMULATE_DONE;
}

static int _kvm_trap_handle_gspr(struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	struct kvm_run *run = vcpu->run;
	larch_inst inst;
	unsigned long curr_pc;
	int rd, rj;
	unsigned int index;

	/*
	 *  Fetch the instruction.
	 */
	inst.word = vcpu->arch.badi;
	curr_pc = vcpu->arch.pc;
	update_pc(&vcpu->arch);

	trace_kvm_exit_gspr(vcpu, inst.word);
	er = EMULATE_FAIL;
	switch (((inst.word >> 24) & 0xff)) {
	case 0x0:
		/* cpucfg GSPR */
		if (inst.reg2_format.opcode == 0x1B) {
			rd = inst.reg2_format.rd;
			rj = inst.reg2_format.rj;
			++vcpu->stat.cpucfg_exits;
			index = vcpu->arch.gprs[rj];

			vcpu->arch.gprs[rd] = read_cpucfg(index);
			/* Nested KVM is not supported */
			if (index == 2)
				vcpu->arch.gprs[rd] &= ~CPUCFG2_LVZP;
			if (index == 6)
				vcpu->arch.gprs[rd] &= ~CPUCFG6_PMP;
			er = EMULATE_DONE;
		}
		break;
	case 0x4:
		/* csr GSPR */
		er = _kvm_handle_csr(vcpu, inst);
		break;
	case 0x6:
		/* iocsr,cache,idle GSPR */
		switch (((inst.word >> 22) & 0x3ff)) {
		case 0x18:
			/* cache GSPR */
			er = EMULATE_DONE;
			trace_kvm_exit_cache(vcpu, KVM_TRACE_EXIT_CACHE);
			break;
		case 0x19:
			/* iocsr/idle GSPR */
			switch (((inst.word >> 15) & 0x1ffff)) {
			case 0xc90:
				/* iocsr GSPR */
				er = _kvm_emu_iocsr(inst, run, vcpu);
				break;
			case 0xc91:
				/* idle GSPR */
				er = _kvm_emu_idle(vcpu);
				break;
			default:
				er = EMULATE_FAIL;
				break;
			}
			break;
		default:
			er = EMULATE_FAIL;
			break;
		}
		break;
	default:
		er = EMULATE_FAIL;
		break;
	}

	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL) {
		kvm_err("[%#lx]%s: unsupported gspr instruction 0x%08x\n",
			curr_pc, __func__, inst.word);

		kvm_arch_vcpu_dump_regs(vcpu);
		vcpu->arch.pc = curr_pc;
	}
	return er;
}

/*
 * Execute cpucfg instruction will tirggerGSPR,
 * Also the access to unimplemented csrs 0x15
 * 0x16, 0x50~0x53, 0x80, 0x81, 0x90~0x95, 0x98
 * 0xc0~0xff, 0x100~0x109, 0x500~0x502,
 * cache_op, idle_op iocsr ops the same
 */
static int _kvm_handle_gspr(struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	int ret = RESUME_GUEST;

	er = _kvm_trap_handle_gspr(vcpu);

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_IOCSR) {
		vcpu->run->exit_reason = KVM_EXIT_LOONGARCH_IOCSR;
		ret = RESUME_HOST;
	} else {
		kvm_err("%s internal error\n", __func__);
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}
