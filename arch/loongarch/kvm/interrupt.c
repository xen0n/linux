// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <asm/kvm_vcpu.h>
#include <asm/kvm_csr.h>

static unsigned int int_to_coreint[EXCCODE_INT_NUM] = {
	[INT_TI]	= CPU_TIMER,
	[INT_IPI]	= CPU_IPI,
	[INT_SWI0]	= CPU_SIP0,
	[INT_SWI1]	= CPU_SIP1,
	[INT_HWI0]	= CPU_IP0,
	[INT_HWI1]	= CPU_IP1,
	[INT_HWI2]	= CPU_IP2,
	[INT_HWI3]	= CPU_IP3,
	[INT_HWI4]	= CPU_IP4,
	[INT_HWI5]	= CPU_IP5,
	[INT_HWI6]	= CPU_IP6,
	[INT_HWI7]	= CPU_IP7,
};

static int _kvm_irq_deliver(struct kvm_vcpu *vcpu, unsigned int priority)
{
	unsigned int irq = 0;

	clear_bit(priority, &vcpu->arch.irq_pending);
	if (priority < EXCCODE_INT_NUM)
		irq = int_to_coreint[priority];

	switch (priority) {
	case INT_TI:
	case INT_IPI:
	case INT_SWI0:
	case INT_SWI1:
		set_gcsr_estat(irq);
		break;

	case INT_HWI0 ... INT_HWI7:
		set_csr_gintc(irq);
		break;

	default:
		break;
	}

	return 1;
}

static int _kvm_irq_clear(struct kvm_vcpu *vcpu, unsigned int priority)
{
	unsigned int irq = 0;

	clear_bit(priority, &vcpu->arch.irq_clear);
	if (priority < EXCCODE_INT_NUM)
		irq = int_to_coreint[priority];

	switch (priority) {
	case INT_TI:
	case INT_IPI:
	case INT_SWI0:
	case INT_SWI1:
		clear_gcsr_estat(irq);
		break;

	case INT_HWI0 ... INT_HWI7:
		clear_csr_gintc(irq);
		break;

	default:
		break;
	}

	return 1;
}

void _kvm_deliver_intr(struct kvm_vcpu *vcpu)
{
	unsigned long *pending = &vcpu->arch.irq_pending;
	unsigned long *pending_clr = &vcpu->arch.irq_clear;
	unsigned int priority;

	if (!(*pending) && !(*pending_clr))
		return;

	if (*pending_clr) {
		priority = __ffs(*pending_clr);
		while (priority <= INT_IPI) {
			_kvm_irq_clear(vcpu, priority);
			priority = find_next_bit(pending_clr,
					BITS_PER_BYTE * sizeof(*pending_clr),
					priority + 1);
		}
	}

	if (*pending) {
		priority = __ffs(*pending);
		while (priority <= INT_IPI) {
			_kvm_irq_deliver(vcpu, priority);
			priority = find_next_bit(pending,
					BITS_PER_BYTE * sizeof(*pending),
					priority + 1);
		}
	}
}

int _kvm_pending_timer(struct kvm_vcpu *vcpu)
{
	return test_bit(INT_TI, &vcpu->arch.irq_pending);
}
