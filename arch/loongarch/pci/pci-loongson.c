// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/vgaarb.h>

#include <asm/numa.h>
#include <asm/loongson.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

static int loongson_pci_config_access(unsigned char access_type,
		struct pci_bus *bus, unsigned int devfn,
		int where, u32 *data)
{
	int node = pcibus_to_node(bus);
	int device = PCI_SLOT(devfn);
	int function = PCI_FUNC(devfn);
	int reg = where & ~3;
	unsigned char busnum = pci_is_root_bus(bus) ? 0 : bus->number;
	u_int64_t addr;
	void *addrp;

	/*
	 * Filter out non-supported devices.
	 * device 2: misc, device 21: confbus
	 */
	if (where < PCI_CFG_SPACE_SIZE) { /* standard config */
		addr = nid_to_addrbase(node) | (busnum << 16) | (device << 11) | (function << 8) | reg;
		if (busnum == 0) {
			if (device > 23 || (device >= 9 && device <= 20 && function == 1))
				return PCIBIOS_DEVICE_NOT_FOUND;
			addrp = (void *)TO_UNCAC(HT1LO_PCICFG_BASE | addr);
		} else {
			addrp = (void *)TO_UNCAC(HT1LO_PCICFG_BASE_TP1 | addr);
		}
	} else if (where < PCI_CFG_SPACE_EXP_SIZE) {  /* extended config */
		reg = (reg & 0xff) | ((reg & 0xf00) << 16);
		addr = nid_to_addrbase(node) | (busnum << 16) | (device << 11) | (function << 8) | reg;
		if (busnum == 0) {
			if (device > 23 || (device >= 9 && device <= 20 && function == 1))
				return PCIBIOS_DEVICE_NOT_FOUND;
			addrp = (void *)TO_UNCAC(HT1LO_EXT_PCICFG_BASE | addr);
		} else {
			addrp = (void *)TO_UNCAC(HT1LO_EXT_PCICFG_BASE_TP1 | addr);
		}
	} else {
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (access_type == PCI_ACCESS_WRITE)
		writel(*data, addrp);
	else {
		*data = readl(addrp);
		if (*data == 0xffffffff) {
			*data = -1;
			return PCIBIOS_DEVICE_NOT_FOUND;
		}
	}

	return PCIBIOS_SUCCESSFUL;
}

int loongson_pci_read(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val)
{
	u32 data = 0;
	int ret = loongson_pci_config_access(PCI_ACCESS_READ,
			bus, devfn, where, &data);

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return ret;
}

int loongson_pci_write(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val)
{
	int ret;
	u32 data = 0;

	if (size == 4)
		data = val;
	else {
		ret = loongson_pci_config_access(PCI_ACCESS_READ,
				bus, devfn, where, &data);
		if (ret != PCIBIOS_SUCCESSFUL)
			return ret;

		if (size == 1)
			data = (data & ~(0xff << ((where & 3) << 3))) |
			    (val << ((where & 3) << 3));
		else if (size == 2)
			data = (data & ~(0xffff << ((where & 3) << 3))) |
			    (val << ((where & 3) << 3));
	}

	ret = loongson_pci_config_access(PCI_ACCESS_WRITE,
			bus, devfn, where, &data);

	return ret;
}

static void pci_root_fix_resbase(struct pci_dev *dev)
{
	int node = pcibus_to_node(dev->bus);
	struct resource *res;
	struct resource_entry *entry, *window, *tmp;
	struct pci_host_bridge *bridge = to_pci_host_bridge(dev->bus->bridge);

	resource_list_for_each_entry_safe(entry, tmp, &dev->bus->resources) {
		res = entry->res;

		if (res->flags & IORESOURCE_MEM) {
			res->start &= GENMASK_ULL(40, 0);
			res->end   &= GENMASK_ULL(40, 0);
		}
	}

	resource_list_for_each_entry(window, &bridge->windows) {
		if (window->res->flags & IORESOURCE_MEM) {
			window->offset = nid_to_addrbase(node) | HT1LO_OFFSET;
			window->res->start |= nid_to_addrbase(node) | HT1LO_OFFSET;
			window->res->end   |= nid_to_addrbase(node) | HT1LO_OFFSET;
		}
	}
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_HOST, pci_root_fix_resbase);

static void pci_fixup_vgadev(struct pci_dev *pdev)
{
	struct pci_dev *devp = NULL;

	while ((devp = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, devp))) {
		if (devp->vendor != PCI_VENDOR_ID_LOONGSON) {
			vga_set_default_device(devp);
			dev_info(&pdev->dev,
				"Overriding boot device as %X:%X\n",
				devp->vendor, devp->device);
		}
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC, pci_fixup_vgadev);
