/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_PCI_H
#define _ASM_PCI_H

#include <linux/mm.h>

#ifdef __KERNEL__

/*
 * This file essentially defines the interface between board specific
 * PCI code and LoongArch common PCI code. Should potentially put into
 * include/asm/pci.h file.
 */

#include <linux/ioport.h>
#include <linux/list.h>

/*
 * Each pci channel is a top-level PCI bus seem by CPU.	 A machine  with
 * multiple PCI channels may have multiple PCI host controllers or a
 * single controller supporting multiple channels.
 */
struct pci_controller {
	struct list_head list;
	struct pci_bus *bus;
	struct device_node *of_node;

	struct pci_ops *pci_ops;
	struct resource *mem_resource;
	unsigned long mem_offset;
	struct resource *io_resource;
	unsigned long io_offset;
	struct resource *busn_resource;

	unsigned int node;
	unsigned int index;
	unsigned int need_domain_info;
#ifdef CONFIG_ACPI
	struct acpi_device *companion;
#endif
	phys_addr_t mcfg_addr;
};

extern void pcibios_add_root_resources(struct list_head *resources);

extern phys_addr_t mcfg_addr_init(int domain);

static inline void set_pci_need_domain_info(struct pci_controller *hose,
					    int need_domain_info)
{
	hose->need_domain_info = need_domain_info;
}

/*
 * Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader
 */
static inline unsigned int pcibios_assign_all_busses(void)
{
	return 0;
}

#define PCIBIOS_MIN_IO		0x4000
#define PCIBIOS_MIN_MEM		0x20000000
#define PCIBIOS_MIN_CARDBUS_IO	0x4000

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE
#define HAVE_ARCH_PCI_RESOURCE_TO_USER

/*
 * Dynamic DMA mapping stuff.
 * LoongArch has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

#define pci_domain_nr(bus) (((struct pci_controller *)(bus)->sysdata)->index)

static inline int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;

	return hose->need_domain_info;
}

/* mmconfig.c */

#ifdef CONFIG_PCI_MMCONFIG
struct pci_mmcfg_region {
	struct list_head list;
	phys_addr_t address;
	u16 segment;
	u8 start_bus;
	u8 end_bus;
};

extern phys_addr_t pci_mmconfig_addr(u16 seg, u8 start);
extern int pci_mmconfig_delete(u16 seg, u8 start, u8 end);
extern struct pci_mmcfg_region *pci_mmconfig_lookup(int segment, int bus);
extern struct list_head pci_mmcfg_list;
#endif /*CONFIG_PCI_MMCONFIG*/

#endif /* __KERNEL__ */

int loongson_pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val);
int loongson_pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val);

/* generic pci stuff */
#include <asm-generic/pci.h>

#endif /* _ASM_PCI_H */
