// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

static bool msix_enable = 1;
core_param(msix, msix_enable, bool, 0664);

int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct irq_domain *msi_domain;

	if (!pci_msi_enabled())
		return -ENOSPC;

	if (type == PCI_CAP_ID_MSIX && !msix_enable)
		return -ENOSPC;

	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	msi_domain = irq_find_matching_fwnode(acpi_msidomain_handle, DOMAIN_BUS_PCI_MSI);
	if (!msi_domain)
		return -ENOSPC;

	return msi_domain_alloc_irqs(msi_domain, &dev->dev, nvec);

}

void arch_teardown_msi_irq(unsigned int irq)
{
	struct irq_domain *msi_domain;

	msi_domain = irq_find_matching_fwnode(acpi_msidomain_handle, DOMAIN_BUS_PCI_MSI);
	if (msi_domain)
		irq_domain_free_irqs(irq, 1);
}
