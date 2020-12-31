// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/of_address.h>
#include <asm/loongson.h>

int raw_pci_read(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 *val)
{
	struct pci_bus *bus_tmp = pci_find_bus(domain, bus);

	if (bus_tmp)
		return bus_tmp->ops->read(bus_tmp, devfn, reg, len, val);
	return -EINVAL;
}

int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
						int reg, int len, u32 val)
{
	struct pci_bus *bus_tmp = pci_find_bus(domain, bus);

	if (bus_tmp)
		return bus_tmp->ops->write(bus_tmp, devfn, reg, len, val);
	return -EINVAL;
}

void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc, resource_size_t *start,
			  resource_size_t *end)
{
	phys_addr_t size = resource_size(rsrc);

	*start = rsrc->start;
	*end = rsrc->start + size - 1;
}

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might have be mirrored at 0x0100-0x03ff..
 */
resource_size_t
pcibios_align_resource(void *data, const struct resource *res,
		       resource_size_t size, resource_size_t align)
{
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		/*
		 * Put everything into 0x00-0xff region modulo 0x400
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	} else if (res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start < PCIBIOS_MIN_MEM)
			start = PCIBIOS_MIN_MEM;
	}

	return start;
}

phys_addr_t mcfg_addr_init(int domain)
{
	return (((u64) domain << 44) | MCFG_EXT_PCICFG_BASE);
}

static struct resource pci_mem_resource = {
	.name	= "pci memory space",
	.flags	= IORESOURCE_MEM,
};

static struct resource pci_io_resource = {
	.name	= "pci io space",
	.flags	= IORESOURCE_IO,
};

void pcibios_add_root_resources(struct list_head *resources)
{
	if (resources) {
		pci_add_resource(resources, &pci_mem_resource);
		pci_add_resource(resources, &pci_io_resource);
	}
}

static int __init pcibios_set_cache_line_size(void)
{
	unsigned int lsize;

	/*
	 * Set PCI cacheline size to that of the highest level in the
	 * cache hierarchy.
	 */
	lsize = cpu_dcache_line_size();
	lsize = cpu_vcache_line_size() ? : lsize;
	lsize = cpu_scache_line_size() ? : lsize;

	BUG_ON(!lsize);

	pci_dfl_cache_line_size = lsize >> 2;

	pr_debug("PCI: pci_cache_line_size set to %d bytes\n", lsize);

	return 0;
}

static int __init pcibios_init(void)
{
	return pcibios_set_cache_line_size();
}

subsys_initcall(pcibios_init);

int pcibios_dev_init(struct pci_dev *dev)
{
#ifdef CONFIG_ACPI
	if (acpi_disabled)
		return 0;
	if (pci_dev_msi_enabled(dev))
		return 0;
	return acpi_pci_irq_enable(dev);
#endif
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	err = pci_enable_resources(dev, mask);
	if (err < 0)
		return err;

	return pcibios_dev_init(dev);
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;

	if (pci_has_flag(PCI_PROBE_ONLY) && dev &&
	    (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_read_bridge_bases(bus);
	}
}
