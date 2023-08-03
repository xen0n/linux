// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Loongson Corporation
 */

#include <linux/clk-provider.h>
#include <linux/pci.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include "dwmac1000.h"
#include "stmmac.h"

struct stmmac_pci_info {
	int (*setup)(struct pci_dev *pdev, struct plat_stmmacenet_data *plat);
	int (*config)(struct pci_dev *pdev, struct plat_stmmacenet_data *plat,
		      struct stmmac_resources *res);
};

static void loongson_default_data(struct pci_dev *pdev,
				  struct plat_stmmacenet_data *plat)
{
	/* Get bus_id, this can be overloaded later */
	plat->bus_id = (pci_domain_nr(pdev->bus) << 16) |
		       PCI_DEVID(pdev->bus->number, pdev->devfn);

	plat->clk_csr = 2;	/* clk_csr_i = 20-35MHz & MDC = clk_csr_i/16 */
	plat->has_gmac = 1;
	plat->force_sf_dma_mode = 1;

	/* Set default value for multicast hash bins */
	plat->multicast_filter_bins = HASH_TABLE_SIZE;

	/* Set default value for unicast filter entries */
	plat->unicast_filter_entries = 1;

	/* Set the maxmtu to a default of JUMBO_LEN */
	plat->maxmtu = JUMBO_LEN;

	/* Set default number of RX and TX queues to use */
	plat->tx_queues_to_use = 1;
	plat->rx_queues_to_use = 1;

	/* Disable Priority config by default */
	plat->tx_queues_cfg[0].use_prio = false;
	plat->rx_queues_cfg[0].use_prio = false;

	/* Disable RX queues routing by default */
	plat->rx_queues_cfg[0].pkt_route = 0x0;
}

static int loongson_gmac_data(struct pci_dev *pdev,
			      struct plat_stmmacenet_data *plat)
{
	loongson_default_data(pdev, plat);

	plat->multicast_filter_bins = 256;

	plat->mdio_bus_data->phy_mask = 0;

	plat->phy_addr = -1;
	plat->phy_interface = PHY_INTERFACE_MODE_RGMII_ID;

	plat->dma_cfg->pbl = 32;
	plat->dma_cfg->pblx8 = true;

	plat->clk_ref_rate = 125000000;
	plat->clk_ptp_rate = 125000000;

	return 0;
}

static int loongson_gmac_config(struct pci_dev *pdev,
				struct plat_stmmacenet_data *plat,
				struct stmmac_resources *res)
{
	u32 version = readl(res->addr + GMAC_VERSION);

	switch (version & 0xff) {
	case DWLGMAC_CORE_1_00:
		plat->flags |= STMMAC_FLAG_MULTI_MSI_EN;
		plat->rx_queues_to_use = 8;
		plat->tx_queues_to_use = 8;
		plat->fix_channel_num = true;
		break;
	case DWMAC_CORE_3_50:
	case DWMAC_CORE_3_70:
		if (version & 0x00008000) {
			plat->host_dma_width = 64;
			plat->dma_cfg->dma64 = true;
		}
		break;
	default:
		break;
	}

	plat->dma_reset_times = 5;
	plat->disable_flow_control = true;

	return 0;
}

static struct stmmac_pci_info loongson_gmac_pci_info = {
	.setup = loongson_gmac_data,
	.config = loongson_gmac_config,
};

static u32 get_irq_type(struct device_node *np)
{
	struct of_phandle_args oirq;

	if (np && of_irq_parse_one(np, 0, &oirq) == 0 && oirq.args_count == 2)
		return oirq.args[1];

	return IRQF_TRIGGER_RISING;
}

static int loongson_dwmac_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	int ret, i, bus_id, phy_mode, ch_cnt, vecs;
	struct plat_stmmacenet_data *plat;
	struct stmmac_pci_info *info;
	struct stmmac_resources res;
	struct device_node *np;

	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (!plat)
		return -ENOMEM;

	plat->mdio_bus_data = devm_kzalloc(&pdev->dev,
					   sizeof(*plat->mdio_bus_data),
					   GFP_KERNEL);
	if (!plat->mdio_bus_data)
		return -ENOMEM;

	plat->dma_cfg = devm_kzalloc(&pdev->dev, sizeof(*plat->dma_cfg),
				     GFP_KERNEL);
	if (!plat->dma_cfg)
		return -ENOMEM;

	np = dev_of_node(&pdev->dev);
	plat->mdio_node = of_get_child_by_name(np, "mdio");
	if (plat->mdio_node) {
		dev_info(&pdev->dev, "Found MDIO subnode\n");
		plat->mdio_bus_data->needs_reset = true;
	}

	/* Enable pci device */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: ERROR: failed to enable device\n",
			__func__);
		goto err_put_node;
	}

	/* Get the base address of device */
	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
		if (ret)
			goto err_disable_device;
		break;
	}

	pci_set_master(pdev);

	info = (struct stmmac_pci_info *)id->driver_data;
	ret = info->setup(pdev, plat);
	if (ret)
		goto err_disable_device;

	if (np) {
		bus_id = of_alias_get_id(np, "ethernet");
		if (bus_id >= 0)
			plat->bus_id = bus_id;

		phy_mode = device_get_phy_mode(&pdev->dev);
		if (phy_mode < 0) {
			dev_err(&pdev->dev, "phy_mode not found\n");
			ret = phy_mode;
			goto err_disable_device;
		}
		plat->phy_interface = phy_mode;
		plat->mac_interface = PHY_INTERFACE_MODE_GMII;
	}

	pci_enable_msi(pdev);

	memset(&res, 0, sizeof(res));
	res.addr = pcim_iomap_table(pdev)[0];
	if (np) {
		res.irq = of_irq_get_byname(np, "macirq");
		if (res.irq < 0) {
			dev_err(&pdev->dev, "IRQ macirq not found\n");
			ret = -ENODEV;
			goto err_disable_msi;
		}

		res.wol_irq = of_irq_get_byname(np, "eth_wake_irq");
		if (res.wol_irq < 0) {
			dev_info(&pdev->dev,
				 "IRQ eth_wake_irq not found, using macirq\n");
			res.wol_irq = res.irq;
		}

		res.lpi_irq = of_irq_get_byname(np, "eth_lpi");
		if (res.lpi_irq < 0) {
			dev_err(&pdev->dev, "IRQ eth_lpi not found\n");
			ret = -ENODEV;
			goto err_disable_msi;
		}
	} else {
		res.irq = pdev->irq;
		res.wol_irq = pdev->irq;
	}

	ret = info->config(pdev, plat, &res);
	if (ret)
		goto err_disable_msi;

	if (plat->flags & STMMAC_FLAG_MULTI_MSI_EN) {
		ch_cnt = plat->rx_queues_to_use;

		pci_disable_msi(pdev);

		res.irq = pci_irq_vector(pdev, 0);
		res.wol_irq = res.irq;
		vecs = roundup_pow_of_two(ch_cnt * 2 + 1);
		if (pci_alloc_irq_vectors(pdev, vecs, vecs, PCI_IRQ_MSI) < 0) {
			dev_info(&pdev->dev,
				 "MSI enable failed, Fallback to line interrupt\n");
			plat->flags &= ~STMMAC_FLAG_MULTI_MSI_EN;
		} else {
			/* INT NAME | MAC | CH7 rx | CH7 tx | ... | CH0 rx | CH0 tx |
			 * --------- ----- -------- --------  ...  -------- --------
			 * IRQ NUM  |  0  |   1    |   2    | ... |   15   |   16   |
			 */
			for (i = 0; i < ch_cnt; i++) {
				res.rx_irq[ch_cnt - 1 - i] = pci_irq_vector(pdev, 1 + i * 2);
				res.tx_irq[ch_cnt - 1 - i] = pci_irq_vector(pdev, 2 + i * 2);
			}

			plat->control_value = GMAC_CONTROL_ACS;
			plat->irq_flags = get_irq_type(np);
		}
	}

	ret = stmmac_dvr_probe(&pdev->dev, plat, &res);
	if (ret)
		goto err_free_irq_vectors;

	return ret;

err_free_irq_vectors:
	if (plat->flags & STMMAC_FLAG_MULTI_MSI_EN)
		pci_free_irq_vectors(pdev);
err_disable_msi:
	pci_disable_msi(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_put_node:
	of_node_put(plat->mdio_node);
	return ret;
}

static void loongson_dwmac_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int i;

	of_node_put(priv->plat->mdio_node);
	stmmac_dvr_remove(&pdev->dev);

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		if (pci_resource_len(pdev, i) == 0)
			continue;
		pcim_iounmap_regions(pdev, BIT(i));
		break;
	}

	pci_free_irq_vectors(pdev);
	pci_disable_msi(pdev);
	pci_disable_device(pdev);
}

static int __maybe_unused loongson_dwmac_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	ret = stmmac_suspend(dev);
	if (ret)
		return ret;

	ret = pci_save_state(pdev);
	if (ret)
		return ret;

	pci_disable_device(pdev);
	pci_wake_from_d3(pdev, true);
	return 0;
}

static int __maybe_unused loongson_dwmac_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;

	pci_restore_state(pdev);
	pci_set_power_state(pdev, PCI_D0);

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_master(pdev);

	return stmmac_resume(dev);
}

static SIMPLE_DEV_PM_OPS(loongson_dwmac_pm_ops, loongson_dwmac_suspend,
			 loongson_dwmac_resume);

#define PCI_DEVICE_ID_LOONGSON_GMAC	0x7a03

static const struct pci_device_id loongson_dwmac_id_table[] = {
	{ PCI_DEVICE_DATA(LOONGSON, GMAC, &loongson_gmac_pci_info) },
	{}
};
MODULE_DEVICE_TABLE(pci, loongson_dwmac_id_table);

static struct pci_driver loongson_dwmac_driver = {
	.name = "dwmac-loongson-pci",
	.id_table = loongson_dwmac_id_table,
	.probe = loongson_dwmac_probe,
	.remove = loongson_dwmac_remove,
	.driver = {
		.pm = &loongson_dwmac_pm_ops,
	},
};

module_pci_driver(loongson_dwmac_driver);

MODULE_DESCRIPTION("Loongson DWMAC PCI driver");
MODULE_AUTHOR("Qing Zhang <zhangqing@loongson.cn>");
MODULE_LICENSE("GPL v2");
