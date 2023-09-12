// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <linux/entry-kvm.h>
#include <asm/fpu.h>
#include <asm/loongarch.h>
#include <asm/setup.h>
#include <asm/time.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

int kvm_arch_vcpu_precreate(struct kvm *kvm, unsigned int id)
{
	return 0;
}

/*
 * _kvm_check_requests - check and handle pending vCPU requests
 *
 * Return: RESUME_GUEST if we should enter the guest
 *         RESUME_HOST  if we should exit to userspace
 */
static int _kvm_check_requests(struct kvm_vcpu *vcpu)
{
	if (!kvm_request_pending(vcpu))
		return RESUME_GUEST;

	if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu))
		/* Drop vpid for this vCPU */
		vcpu->arch.vpid = 0;

	if (kvm_dirty_ring_check_request(vcpu))
		return RESUME_HOST;

	return RESUME_GUEST;
}

/*
 * Check and handle pending signal and vCPU requests etc
 * Run with irq enabled and preempt enabled
 *
 * Return: RESUME_GUEST if we should enter the guest
 *         RESUME_HOST  if we should exit to userspace
 *         < 0 if we should exit to userspace, where the return value
 *         indicates an error
 */
static int kvm_enter_guest_check(struct kvm_vcpu *vcpu)
{
	int ret;

	/*
	 * Check conditions before entering the guest
	 */
	ret = xfer_to_guest_mode_handle_work(vcpu);
	if (ret < 0)
		return ret;

	ret = _kvm_check_requests(vcpu);
	return ret;
}

/*
 * called with irq enabled
 *
 * Return: RESUME_GUEST if we should enter the guest, and irq disabled
 *         Others if we should exit to userspace
 */
static int kvm_pre_enter_guest(struct kvm_vcpu *vcpu)
{
	int ret;

	do {
		ret = kvm_enter_guest_check(vcpu);
		if (ret != RESUME_GUEST)
			break;

		/*
		 * handle vcpu timer, interrupts, check requests and
		 * check vmid before vcpu enter guest
		 */
		local_irq_disable();
		kvm_acquire_timer(vcpu);
		_kvm_deliver_intr(vcpu);
		/* make sure the vcpu mode has been written */
		smp_store_mb(vcpu->mode, IN_GUEST_MODE);
		_kvm_check_vmid(vcpu);
		vcpu->arch.host_eentry = csr_read64(LOONGARCH_CSR_EENTRY);
		/* clear KVM_LARCH_CSR as csr will change when enter guest */
		vcpu->arch.aux_inuse &= ~KVM_LARCH_CSR;

		if (kvm_request_pending(vcpu) || xfer_to_guest_mode_work_pending()) {
			/* make sure the vcpu mode has been written */
			smp_store_mb(vcpu->mode, OUTSIDE_GUEST_MODE);
			local_irq_enable();
			ret = -EAGAIN;
		}
	} while (ret != RESUME_GUEST);

	return ret;
}

/*
 * Return 1 for resume guest and "<= 0" for resume host.
 */
static int _kvm_handle_exit(struct kvm_run *run, struct kvm_vcpu *vcpu)
{
	unsigned long exst = vcpu->arch.host_estat;
	u32 intr = exst & 0x1fff; /* ignore NMI */
	u32 exccode = (exst & CSR_ESTAT_EXC) >> CSR_ESTAT_EXC_SHIFT;
	int ret = RESUME_GUEST;

	vcpu->mode = OUTSIDE_GUEST_MODE;

	/* Set a default exit reason */
	run->exit_reason = KVM_EXIT_UNKNOWN;

	guest_timing_exit_irqoff();
	guest_state_exit_irqoff();
	local_irq_enable();

	trace_kvm_exit(vcpu, exccode);
	if (exccode) {
		ret = _kvm_handle_fault(vcpu, exccode);
	} else {
		WARN(!intr, "vm exiting with suspicious irq\n");
		++vcpu->stat.int_exits;
	}

	if (ret == RESUME_GUEST)
		ret = kvm_pre_enter_guest(vcpu);

	if (ret != RESUME_GUEST) {
		local_irq_disable();
		return ret;
	}

	guest_timing_enter_irqoff();
	guest_state_enter_irqoff();
	trace_kvm_reenter(vcpu);
	return RESUME_GUEST;
}

int kvm_arch_vcpu_create(struct kvm_vcpu *vcpu)
{
	unsigned long timer_hz;
	struct loongarch_csrs *csr;

	vcpu->arch.vpid = 0;

	hrtimer_init(&vcpu->arch.swtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	vcpu->arch.swtimer.function = kvm_swtimer_wakeup;

	vcpu->arch.guest_eentry = (unsigned long)kvm_loongarch_ops->guest_eentry;
	vcpu->arch.handle_exit = _kvm_handle_exit;
	vcpu->arch.csr = kzalloc(sizeof(struct loongarch_csrs), GFP_KERNEL);
	if (!vcpu->arch.csr)
		return -ENOMEM;

	/*
	 * kvm all exceptions share one exception entry, and host <-> guest switch
	 * also switch excfg.VS field, keep host excfg.VS info here
	 */
	vcpu->arch.host_ecfg = (read_csr_ecfg() & CSR_ECFG_VS);

	/* Init */
	vcpu->arch.last_sched_cpu = -1;

	/*
	 * Initialize guest register state to valid architectural reset state.
	 */
	timer_hz = calc_const_freq();
	kvm_init_timer(vcpu, timer_hz);

	/* Set Initialize mode for GUEST */
	csr = vcpu->arch.csr;
	kvm_write_sw_gcsr(csr, LOONGARCH_CSR_CRMD, CSR_CRMD_DA);

	/* Set cpuid */
	kvm_write_sw_gcsr(csr, LOONGARCH_CSR_TMID, vcpu->vcpu_id);

	/* start with no pending virtual guest interrupts */
	csr->csrs[LOONGARCH_CSR_GINTC] = 0;

	return 0;
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	int cpu;
	struct kvm_context *context;

	hrtimer_cancel(&vcpu->arch.swtimer);
	kvm_mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
	kfree(vcpu->arch.csr);

	/*
	 * If the vCPU is freed and reused as another vCPU, we don't want the
	 * matching pointer wrongly hanging around in last_vcpu.
	 */
	for_each_possible_cpu(cpu) {
		context = per_cpu_ptr(vcpu->kvm->arch.vmcs, cpu);
		if (context->last_vcpu == vcpu)
			context->last_vcpu = NULL;
	}
}

int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu)
{
	int r = -EINTR;
	struct kvm_run *run = vcpu->run;

	if (vcpu->mmio_needed) {
		if (!vcpu->mmio_is_write)
			_kvm_complete_mmio_read(vcpu, run);
		vcpu->mmio_needed = 0;
	}

	if (run->exit_reason == KVM_EXIT_LOONGARCH_IOCSR) {
		if (!run->iocsr_io.is_write)
			_kvm_complete_iocsr_read(vcpu, run);
	}

	/* clear exit_reason */
	run->exit_reason = KVM_EXIT_UNKNOWN;
	if (run->immediate_exit)
		return r;

	lose_fpu(1);
	vcpu_load(vcpu);
	kvm_sigset_activate(vcpu);
	r = kvm_pre_enter_guest(vcpu);
	if (r != RESUME_GUEST)
		goto out;

	guest_timing_enter_irqoff();
	guest_state_enter_irqoff();
	trace_kvm_enter(vcpu);
	r = kvm_loongarch_ops->enter_guest(run, vcpu);

	trace_kvm_out(vcpu);
	/*
	 * guest exit is already recorded at _kvm_handle_exit
	 * return val must not be RESUME_GUEST
	 */
	local_irq_enable();
out:
	kvm_sigset_deactivate(vcpu);
	vcpu_put(vcpu);
	return r;
}
