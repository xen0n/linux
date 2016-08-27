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

static void ls2h_start_hc(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_regs __iomem *regs = hcd->regs;

	dev_dbg(&pdev->dev, "start\n");

	/*
	 * The USB host controller must remain in reset.
	 */
	writel(0, &regs->control);
}

static void ls2h_stop_hc(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ohci_regs __iomem *regs = hcd->regs;

	dev_dbg(&pdev->dev, "stop\n");

	/*
	 * Put the USB host controller into reset.
	 */
	writel(0, &regs->control);

}

static int usb_hcd_ls2h_probe(const struct hc_driver *driver,
			      struct platform_device *pdev)
{
	int retval;
	struct usb_hcd *hcd = NULL;

	if (pdev->num_resources != 2) {
		pr_debug("hcd probe: invalid num_resources");
		return -ENODEV;
	}

	if ((pdev->resource[0].flags != IORESOURCE_MEM)
	    || (pdev->resource[1].flags != IORESOURCE_IRQ)) {
		pr_debug("hcd probe: invalid resource type\n");
		return -ENODEV;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, "ls2h");
	if (!hcd)
		return -ENOMEM;
	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;

	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		retval = -EBUSY;
		goto err1;
	}
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		pr_debug("ioremap failed\n");
		retval = -EIO;
		goto err2;
	}

	ls2h_start_hc(pdev);
	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, pdev->resource[1].start, 0);
	if (retval == 0)
		return retval;

	/* Error handling */
	ls2h_stop_hc(pdev);

	iounmap(hcd->regs);

err2:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

err1:
	usb_put_hcd(hcd);
	return retval;
}

static int usb_hcd_ls2h_remove(struct usb_hcd *hcd,
			       struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	ls2h_stop_hc(pdev);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);

	dev_set_drvdata(&pdev->dev, NULL);

	return 0;
}

struct ls2h_usbh_data {
	u8 ports;		/* number of ports on root hub */
	u8 vbus_pin[];		/* port power-control pin */
};

static int ohci_ls2h_start(struct usb_hcd *hcd)
{
	struct ls2h_usbh_data *board = hcd->self.controller->platform_data;
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	ohci->num_ports = board->ports;

	if ((ret = ohci_run(ohci)) < 0) {
		ohci_err("can't start %s", hcd->self.bus_name);
		ohci_stop(hcd);
		return ret;
	}
	return 0;
}

static const struct hc_driver ohci_ls2h_hc_driver = {
	.description = hcd_name,
	.product_desc = "Loongson2H OHCI",
	.hcd_priv_size = sizeof(struct ohci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags = HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start = ohci_ls2h_start,
	.stop = ohci_stop,
	.shutdown = ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ohci_urb_enqueue,
	.urb_dequeue = ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ohci_hub_status_data,
	.hub_control = ohci_hub_control,
#ifdef CONFIG_PM
	.bus_suspend = ohci_bus_suspend,
	.bus_resume = ohci_bus_resume,
#endif
	.start_port_reset = ohci_start_port_reset,
};

static int ohci_hcd_ls2h_drv_probe(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 1);
	return usb_hcd_ls2h_probe(&ohci_ls2h_hc_driver, pdev);
}

static int ohci_hcd_ls2h_drv_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 0);
	return usb_hcd_ls2h_remove(platform_get_drvdata(pdev), pdev);
}

#ifdef CONFIG_PM

static int
ohci_hcd_ls2h_drv_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(hcd->irq);

	return 0;
}

static int ohci_hcd_ls2h_drv_resume(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(hcd->irq);

	return 0;
}
#else
#define ohci_hcd_ls2h_drv_suspend NULL
#define ohci_hcd_ls2h_drv_resume  NULL
#endif

MODULE_ALIAS("ls2h_ohci");

static struct platform_driver ohci_hcd_ls2h_driver = {
	.driver = {
			.name	= "ls2h-ohci",
			.owner	= THIS_MODULE,
		   },
	.probe	= ohci_hcd_ls2h_drv_probe,
	.remove = ohci_hcd_ls2h_drv_remove,
	.resume	= ohci_hcd_ls2h_drv_resume,
	.shutdown	= usb_hcd_platform_shutdown,
	.suspend	= ohci_hcd_ls2h_drv_suspend,
};
