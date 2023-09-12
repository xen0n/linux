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

int kvm_arch_vcpu_runnable(struct kvm_vcpu *vcpu)
{
	return !!(vcpu->arch.irq_pending) &&
		vcpu->arch.mp_state.mp_state == KVM_MP_STATE_RUNNABLE;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return false;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

int kvm_arch_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				  struct kvm_translation *tr)
{
	return -EINVAL;
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return _kvm_pending_timer(vcpu) ||
		kvm_read_hw_gcsr(LOONGARCH_CSR_ESTAT) &
			(1 << INT_TI);
}

int kvm_arch_vcpu_dump_regs(struct kvm_vcpu *vcpu)
{
	int i;

	kvm_debug("vCPU Register Dump:\n");
	kvm_debug("\tpc = 0x%08lx\n", vcpu->arch.pc);
	kvm_debug("\texceptions: %08lx\n", vcpu->arch.irq_pending);

	for (i = 0; i < 32; i += 4) {
		kvm_debug("\tgpr%02d: %08lx %08lx %08lx %08lx\n", i,
		       vcpu->arch.gprs[i],
		       vcpu->arch.gprs[i + 1],
		       vcpu->arch.gprs[i + 2], vcpu->arch.gprs[i + 3]);
	}

	kvm_debug("\tCRMOD: 0x%08lx, exst: 0x%08lx\n",
		  kvm_read_hw_gcsr(LOONGARCH_CSR_CRMD),
		  kvm_read_hw_gcsr(LOONGARCH_CSR_ESTAT));

	kvm_debug("\tERA: 0x%08lx\n", kvm_read_hw_gcsr(LOONGARCH_CSR_ERA));

	return 0;
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				struct kvm_mp_state *mp_state)
{
	*mp_state = vcpu->arch.mp_state;

	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				struct kvm_mp_state *mp_state)
{
	int ret = 0;

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_RUNNABLE:
		vcpu->arch.mp_state = *mp_state;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

int kvm_arch_vcpu_ioctl_set_guest_debug(struct kvm_vcpu *vcpu,
					struct kvm_guest_debug *dbg)
{
	return -EINVAL;
}

/**
 * kvm_migrate_count() - Migrate timer.
 * @vcpu:       Virtual CPU.
 *
 * Migrate hrtimer to the current CPU by cancelling and restarting it
 * if it was running prior to being cancelled.
 *
 * Must be called when the vCPU is migrated to a different CPU to ensure that
 * timer expiry during guest execution interrupts the guest and causes the
 * interrupt to be delivered in a timely manner.
 */
static void kvm_migrate_count(struct kvm_vcpu *vcpu)
{
	if (hrtimer_cancel(&vcpu->arch.swtimer))
		hrtimer_restart(&vcpu->arch.swtimer);
}

int _kvm_getcsr(struct kvm_vcpu *vcpu, unsigned int id, u64 *v)
{
	unsigned long val;
	struct loongarch_csrs *csr = vcpu->arch.csr;

	if (get_gcsr_flag(id) & INVALID_GCSR)
		return -EINVAL;

	if (id == LOONGARCH_CSR_ESTAT) {
		/* interrupt status IP0 -- IP7 from GINTC */
		val = kvm_read_sw_gcsr(csr, LOONGARCH_CSR_GINTC) & 0xff;
		*v = kvm_read_sw_gcsr(csr, id) | (val << 2);
		return 0;
	}

	/*
	 * get software csr state if csrid is valid, since software
	 * csr state is consistent with hardware
	 */
	*v = kvm_read_sw_gcsr(csr, id);

	return 0;
}

int _kvm_setcsr(struct kvm_vcpu *vcpu, unsigned int id, u64 val)
{
	struct loongarch_csrs *csr = vcpu->arch.csr;
	int ret = 0, gintc;

	if (get_gcsr_flag(id) & INVALID_GCSR)
		return -EINVAL;

	if (id == LOONGARCH_CSR_ESTAT) {
		/* estat IP0~IP7 inject through guestexcept */
		gintc = (val >> 2) & 0xff;
		write_csr_gintc(gintc);
		kvm_set_sw_gcsr(csr, LOONGARCH_CSR_GINTC, gintc);

		gintc = val & ~(0xffUL << 2);
		write_gcsr_estat(gintc);
		kvm_set_sw_gcsr(csr, LOONGARCH_CSR_ESTAT, gintc);

		return ret;
	}

	if (get_gcsr_flag(id) & HW_GCSR) {
		set_hw_gcsr(id, val);
		/* write sw gcsr to keep consistent with hardware */
		kvm_write_sw_gcsr(csr, id, val);
	} else
		kvm_write_sw_gcsr(csr, id, val);

	return ret;
}

static int _kvm_get_one_reg(struct kvm_vcpu *vcpu,
		const struct kvm_one_reg *reg, s64 *v)
{
	int reg_idx, ret = 0;

	if ((reg->id & KVM_REG_LOONGARCH_MASK) == KVM_REG_LOONGARCH_CSR) {
		reg_idx = KVM_GET_IOC_CSRIDX(reg->id);
		ret = _kvm_getcsr(vcpu, reg_idx, v);
	} else if (reg->id == KVM_REG_LOONGARCH_COUNTER)
		*v = drdtime() + vcpu->kvm->arch.time_offset;
	else
		ret = -EINVAL;

	return ret;
}

static int _kvm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	int ret = -EINVAL;
	s64 v;

	if ((reg->id & KVM_REG_SIZE_MASK) != KVM_REG_SIZE_U64)
		return ret;

	if (_kvm_get_one_reg(vcpu, reg, &v))
		return ret;

	return put_user(v, (u64 __user *)(long)reg->addr);
}

static int _kvm_set_one_reg(struct kvm_vcpu *vcpu,
			const struct kvm_one_reg *reg,
			s64 v)
{
	int ret = 0;
	unsigned long flags;
	u64 val;
	int reg_idx;

	val = v;
	if ((reg->id & KVM_REG_LOONGARCH_MASK) == KVM_REG_LOONGARCH_CSR) {
		reg_idx = KVM_GET_IOC_CSRIDX(reg->id);
		ret = _kvm_setcsr(vcpu, reg_idx, val);
	} else if (reg->id == KVM_REG_LOONGARCH_COUNTER) {
		local_irq_save(flags);
		/*
		 * gftoffset is relative with board, not vcpu
		 * only set for the first time for smp system
		 */
		if (vcpu->vcpu_id == 0)
			vcpu->kvm->arch.time_offset = (signed long)(v - drdtime());
		write_csr_gcntc((ulong)vcpu->kvm->arch.time_offset);
		local_irq_restore(flags);
	} else if (reg->id == KVM_REG_LOONGARCH_VCPU_RESET) {
		kvm_reset_timer(vcpu);
		memset(&vcpu->arch.irq_pending, 0, sizeof(vcpu->arch.irq_pending));
		memset(&vcpu->arch.irq_clear, 0, sizeof(vcpu->arch.irq_clear));
	} else
		ret = -EINVAL;

	return ret;
}

static int _kvm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	s64 v;
	int ret = -EINVAL;

	if ((reg->id & KVM_REG_SIZE_MASK) != KVM_REG_SIZE_U64)
		return ret;

	if (get_user(v, (u64 __user *)(long)reg->addr))
		return ret;

	return _kvm_set_one_reg(vcpu, reg, v);
}

int kvm_arch_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				  struct kvm_sregs *sregs)
{
	return -ENOIOCTLCMD;
}

int kvm_arch_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	vcpu_load(vcpu);

	for (i = 0; i < ARRAY_SIZE(vcpu->arch.gprs); i++)
		regs->gpr[i] = vcpu->arch.gprs[i];

	regs->pc = vcpu->arch.pc;

	vcpu_put(vcpu);
	return 0;
}

int kvm_arch_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu, struct kvm_regs *regs)
{
	int i;

	vcpu_load(vcpu);

	for (i = 1; i < ARRAY_SIZE(vcpu->arch.gprs); i++)
		vcpu->arch.gprs[i] = regs->gpr[i];
	vcpu->arch.gprs[0] = 0; /* zero is special, and cannot be set. */
	vcpu->arch.pc = regs->pc;

	vcpu_put(vcpu);
	return 0;
}

static int kvm_vcpu_ioctl_enable_cap(struct kvm_vcpu *vcpu,
				     struct kvm_enable_cap *cap)
{
	/*
	 * FPU is enable by default, do not support any other caps,
	 * and later we will support such as LSX cap.
	 */
	return -EINVAL;
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	long r;

	vcpu_load(vcpu);

	switch (ioctl) {
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;
		if (ioctl == KVM_SET_ONE_REG)
			r = _kvm_set_reg(vcpu, &reg);
		else
			r = _kvm_get_reg(vcpu, &reg);
		break;
	}
	case KVM_ENABLE_CAP: {
		struct kvm_enable_cap cap;

		r = -EFAULT;
		if (copy_from_user(&cap, argp, sizeof(cap)))
			break;
		r = kvm_vcpu_ioctl_enable_cap(vcpu, &cap);
		break;
	}
	default:
		r = -ENOIOCTLCMD;
		break;
	}

	vcpu_put(vcpu);
	return r;
}

int kvm_arch_vcpu_ioctl_get_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	int i = 0;

	/* no need vcpu_load and vcpu_put */
	fpu->fcsr = vcpu->arch.fpu.fcsr;
	fpu->fcc = vcpu->arch.fpu.fcc;
	for (i = 0; i < NUM_FPU_REGS; i++)
		memcpy(&fpu->fpr[i], &vcpu->arch.fpu.fpr[i], FPU_REG_WIDTH / 64);

	return 0;
}

int kvm_arch_vcpu_ioctl_set_fpu(struct kvm_vcpu *vcpu, struct kvm_fpu *fpu)
{
	int i = 0;

	/* no need vcpu_load and vcpu_put */
	vcpu->arch.fpu.fcsr = fpu->fcsr;
	vcpu->arch.fpu.fcc = fpu->fcc;
	for (i = 0; i < NUM_FPU_REGS; i++)
		memcpy(&vcpu->arch.fpu.fpr[i], &fpu->fpr[i], FPU_REG_WIDTH / 64);

	return 0;
}

/* Enable FPU for guest and restore context */
void kvm_own_fpu(struct kvm_vcpu *vcpu)
{
	preempt_disable();

	/*
	 * Enable FPU for guest
	 */
	set_csr_euen(CSR_EUEN_FPEN);

	kvm_restore_fpu(&vcpu->arch.fpu);
	vcpu->arch.aux_inuse |= KVM_LARCH_FPU;
	trace_kvm_aux(vcpu, KVM_TRACE_AUX_RESTORE, KVM_TRACE_AUX_FPU);

	preempt_enable();
}

/* Save and disable FPU */
void kvm_lose_fpu(struct kvm_vcpu *vcpu)
{
	preempt_disable();

	if (vcpu->arch.aux_inuse & KVM_LARCH_FPU) {
		kvm_save_fpu(&vcpu->arch.fpu);
		vcpu->arch.aux_inuse &= ~KVM_LARCH_FPU;
		trace_kvm_aux(vcpu, KVM_TRACE_AUX_SAVE, KVM_TRACE_AUX_FPU);

		/* Disable FPU */
		clear_csr_euen(CSR_EUEN_FPEN);
	}

	preempt_enable();
}

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq)
{
	int intr = (int)irq->irq;

	if (intr > 0)
		_kvm_queue_irq(vcpu, intr);
	else if (intr < 0)
		_kvm_dequeue_irq(vcpu, -intr);
	else {
		kvm_err("%s: invalid interrupt ioctl %d\n", __func__, irq->irq);
		return -EINVAL;
	}

	kvm_vcpu_kick(vcpu);
	return 0;
}

long kvm_arch_vcpu_async_ioctl(struct file *filp,
			       unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;

	if (ioctl == KVM_INTERRUPT) {
		struct kvm_interrupt irq;

		if (copy_from_user(&irq, argp, sizeof(irq)))
			return -EFAULT;

		kvm_debug("[%d] %s: irq: %d\n", vcpu->vcpu_id, __func__, irq.irq);

		return kvm_vcpu_ioctl_interrupt(vcpu, &irq);
	}

	return -ENOIOCTLCMD;
}

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
