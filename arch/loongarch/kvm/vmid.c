// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include "trace.h"

static void _kvm_update_vpid(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_context *context;
	unsigned long vpid;

	context = per_cpu_ptr(vcpu->kvm->arch.vmcs, cpu);
	vpid = context->vpid_cache + 1;
	if (!(vpid & vpid_mask)) {
		/* finish round of 64 bit loop */
		if (unlikely(!vpid))
			vpid = vpid_mask + 1;

		/* vpid 0 reserved for root */
		++vpid;

		/* start new vpid cycle */
		kvm_flush_tlb_all();
	}

	context->vpid_cache = vpid;
	vcpu->arch.vpid = vpid;
}

void _kvm_check_vmid(struct kvm_vcpu *vcpu)
{
	struct kvm_context *context;
	bool migrated;
	unsigned long ver, old, vpid;
	int cpu;

	cpu = smp_processor_id();
	/*
	 * Are we entering guest context on a different CPU to last time?
	 * If so, the vCPU's guest TLB state on this CPU may be stale.
	 */
	context = per_cpu_ptr(vcpu->kvm->arch.vmcs, cpu);
	migrated = (vcpu->cpu != cpu);

	/*
	 * Check if our vpid is of an older version
	 *
	 * We also discard the stored vpid if we've executed on
	 * another CPU, as the guest mappings may have changed without
	 * hypervisor knowledge.
	 */
	ver = vcpu->arch.vpid & ~vpid_mask;
	old = context->vpid_cache  & ~vpid_mask;
	if (migrated || (ver != old)) {
		_kvm_update_vpid(vcpu, cpu);
		trace_kvm_vpid_change(vcpu, vcpu->arch.vpid);
		vcpu->cpu = cpu;
	}

	/* Restore GSTAT(0x50).vpid */
	vpid = (vcpu->arch.vpid & vpid_mask)
		<< CSR_GSTAT_GID_SHIFT;
	change_csr_gstat(vpid_mask << CSR_GSTAT_GID_SHIFT, vpid);
}
