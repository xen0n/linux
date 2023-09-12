/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#ifndef __ASM_LOONGARCH_KVM_VCPU_H__
#define __ASM_LOONGARCH_KVM_VCPU_H__

#include <linux/kvm_host.h>
#include <asm/loongarch.h>

/* Controlled by 0x5 guest exst */
#define CPU_SIP0			(_ULCAST_(1))
#define CPU_SIP1			(_ULCAST_(1) << 1)
#define CPU_PMU				(_ULCAST_(1) << 10)
#define CPU_TIMER			(_ULCAST_(1) << 11)
#define CPU_IPI				(_ULCAST_(1) << 12)

/* Controlled by 0x52 guest exception VIP
 * aligned to exst bit 5~12
 */
#define CPU_IP0				(_ULCAST_(1))
#define CPU_IP1				(_ULCAST_(1) << 1)
#define CPU_IP2				(_ULCAST_(1) << 2)
#define CPU_IP3				(_ULCAST_(1) << 3)
#define CPU_IP4				(_ULCAST_(1) << 4)
#define CPU_IP5				(_ULCAST_(1) << 5)
#define CPU_IP6				(_ULCAST_(1) << 6)
#define CPU_IP7				(_ULCAST_(1) << 7)

#define MNSEC_PER_SEC			(NSEC_PER_SEC >> 20)

/* KVM_IRQ_LINE irq field index values */
#define KVM_LOONGSON_IRQ_TYPE_SHIFT	24
#define KVM_LOONGSON_IRQ_TYPE_MASK	0xff
#define KVM_LOONGSON_IRQ_VCPU_SHIFT	16
#define KVM_LOONGSON_IRQ_VCPU_MASK	0xff
#define KVM_LOONGSON_IRQ_NUM_SHIFT	0
#define KVM_LOONGSON_IRQ_NUM_MASK	0xffff

/* Irq_type field */
#define KVM_LOONGSON_IRQ_TYPE_CPU_IP	0
#define KVM_LOONGSON_IRQ_TYPE_CPU_IO	1
#define KVM_LOONGSON_IRQ_TYPE_HT	2
#define KVM_LOONGSON_IRQ_TYPE_MSI	3
#define KVM_LOONGSON_IRQ_TYPE_IOAPIC	4
#define KVM_LOONGSON_IRQ_TYPE_ROUTE	5

/* Out-of-kernel GIC cpu interrupt injection irq_number field */
#define KVM_LOONGSON_IRQ_CPU_IRQ	0
#define KVM_LOONGSON_IRQ_CPU_FIQ	1
#define KVM_LOONGSON_CPU_IP_NUM		8

typedef union loongarch_instruction  larch_inst;
typedef int (*exit_handle_fn)(struct kvm_vcpu *);

int  _kvm_emu_mmio_write(struct kvm_vcpu *vcpu, larch_inst inst);
int  _kvm_emu_mmio_read(struct kvm_vcpu *vcpu, larch_inst inst);
int  _kvm_complete_mmio_read(struct kvm_vcpu *vcpu, struct kvm_run *run);
int  _kvm_complete_iocsr_read(struct kvm_vcpu *vcpu, struct kvm_run *run);
int  _kvm_emu_idle(struct kvm_vcpu *vcpu);
int  _kvm_handle_pv_hcall(struct kvm_vcpu *vcpu);
int  _kvm_pending_timer(struct kvm_vcpu *vcpu);
int  _kvm_handle_fault(struct kvm_vcpu *vcpu, int fault);
void _kvm_deliver_intr(struct kvm_vcpu *vcpu);

void kvm_own_fpu(struct kvm_vcpu *vcpu);
void kvm_lose_fpu(struct kvm_vcpu *vcpu);
void kvm_save_fpu(struct loongarch_fpu *fpu);
void kvm_restore_fpu(struct loongarch_fpu *fpu);
void kvm_restore_fcsr(struct loongarch_fpu *fpu);

void kvm_acquire_timer(struct kvm_vcpu *vcpu);
void kvm_reset_timer(struct kvm_vcpu *vcpu);
void kvm_init_timer(struct kvm_vcpu *vcpu, unsigned long hz);
void kvm_restore_timer(struct kvm_vcpu *vcpu);
void kvm_save_timer(struct kvm_vcpu *vcpu);

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq);
/*
 * Loongarch KVM guest interrupt handling
 */
static inline void _kvm_queue_irq(struct kvm_vcpu *vcpu, unsigned int irq)
{
	set_bit(irq, &vcpu->arch.irq_pending);
	clear_bit(irq, &vcpu->arch.irq_clear);
}

static inline void _kvm_dequeue_irq(struct kvm_vcpu *vcpu, unsigned int irq)
{
	clear_bit(irq, &vcpu->arch.irq_pending);
	set_bit(irq, &vcpu->arch.irq_clear);
}

#endif /* __ASM_LOONGARCH_KVM_VCPU_H__ */
