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
