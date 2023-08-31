# SPDX-License-Identifier: GPL-2.0
#
# Makefile for LOONGARCH KVM support
#

ccflags-y += -I $(srctree)/$(src)

include $(srctree)/virt/kvm/Makefile.kvm

obj-$(CONFIG_KVM) += kvm.o

kvm-y += main.o
kvm-y += vm.o
kvm-y += vmid.o
kvm-y += tlb.o
kvm-y += mmu.o
kvm-y += vcpu.o
kvm-y += exit.o
kvm-y += interrupt.o
kvm-y += timer.o
kvm-y += switch.o
kvm-y += csr_ops.o
