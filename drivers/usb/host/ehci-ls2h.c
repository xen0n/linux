/*
 *  Copyright (C) 2013, Loongson Technology Corporation Limited, Inc.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 */

#include <linux/platform_device.h>

/* FIXME: Power Managment is un-ported so temporarily disable it */
#undef CONFIG_PM

int usb_hcd_ls2h_probe(const struct hc_driver *driver,
		       struct platform_device *pdev)
{
	struct ls2h_usbh_data *pdata;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;

	pr_debug("initializing Loongson2H EHCI USB Controller\n");

	/* Need platform data for setup */
	pdata = (struct ls2h_usbh_data *)pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data for %s.\n", pdev->name);
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n", pdev->name);
		return -ENODEV;
	}
	irq = res->start;

	hcd = usb_create_hcd(driver, &pdev->dev, pdev->name);
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			pdev->name);
		retval = -ENODEV;
		goto err2;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = res->end - res->start + 1;
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		retval = -EBUSY;
		goto err2;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		retval = -EFAULT;
		goto err3;
	}

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval != 0)
		goto err4;
	return retval;

err4:
	iounmap(hcd->regs);
err3:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
err2:
	usb_put_hcd(hcd);
err1:
	dev_err(&pdev->dev, "init %s fail, %d\n", pdev->name, retval);
	return retval;
}

void usb_hcd_ls2h_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_ls2h_reinit(struct ehci_hcd *ehci)
{
	return 0;
}

/* called during probe() after chip reset completes */
static int ehci_ls2h_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0 */
	ehci->caps = hcd->regs + 0x0;
	ehci->regs = hcd->regs + 0x0 +
	    HC_LENGTH(ehci,ehci_readl(ehci, &ehci->caps->hc_capbase));
	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	retval = ehci_ls2h_reinit(ehci);
	return retval;
}

static const struct hc_driver ehci_ls2h_hc_driver = {
	.description = hcd_name,
	.product_desc = "loongson2h EHCI Host Controller",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_ls2h_setup,
	.start = ehci_run,
#ifdef	CONFIG_PM
	.suspend = ehci_bus_suspend,
	.resume = ehci_bus_resume,
#endif
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
};

static int ehci_ls2h_drv_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	return usb_hcd_ls2h_probe(&ehci_ls2h_hc_driver, pdev);
}

static int ehci_ls2h_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_hcd_ls2h_remove(hcd, pdev);

	return 0;
}

MODULE_ALIAS("ls2h-ehci");

static struct platform_driver ehci_ls2h_driver = {
	.probe = ehci_ls2h_drv_probe,
	.remove = ehci_ls2h_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		   .name = "ls2h-ehci",
		   },
};
