// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2020, Jiaxun Yang <jiaxun.yang@flygoat.com>
 *  Loongson IOINTC IRQ support
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/irqchip/chained_irq.h>

#include <boot_param.h>

#define IOINTC_CHIP_IRQ	32
#define IOINTC_NUM_PARENT 4

#define IOINTC_REG_INTx_MAP(x)	(x * 0x1)
#define IOINTC_INTC_CHIP_START	0x20

#define IOINTC_REG_INTC_STATUS	(IOINTC_INTC_CHIP_START + 0x20)
#define IOINTC_REG_INTC_EN_STATUS	(IOINTC_INTC_CHIP_START + 0x04)
#define IOINTC_REG_INTC_ENABLE	(IOINTC_INTC_CHIP_START + 0x08)
#define IOINTC_REG_INTC_DISABLE	(IOINTC_INTC_CHIP_START + 0x0c)
#define IOINTC_REG_INTC_POL	(IOINTC_INTC_CHIP_START + 0x10)
#define IOINTC_REG_INTC_EDGE	(IOINTC_INTC_CHIP_START + 0x14)

#define BUGGY_LPC_IRQ	10

#define IOINTC_SHIFT_INTx	4

struct iointc_handler_data {
	struct iointc_priv *priv;
	u32 parent_int_map;
};

struct iointc_priv {
	void __iomem *base;
	struct irq_chip_generic *gc;
	u8 map_cache[IOINTC_CHIP_IRQ];
	struct iointc_handler_data handler[IOINTC_NUM_PARENT];
	u8 possible_parent_mask;
	bool have_lpc_irq_bug;
};

static void iointc_chained_handle_irq(struct irq_desc *desc)
{
	struct iointc_handler_data *handler = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_chip_generic *gc = handler->priv->gc;
	u32 pending;

	chained_irq_enter(chip, desc);

	pending = readl(gc->reg_base + IOINTC_REG_INTC_STATUS);

	if (!pending) {
		/* Always blame LPC IRQ if we have that Bug and LPC interrupt enabled */
		if (handler->priv->have_lpc_irq_bug &&
			(handler->parent_int_map & ~gc->mask_cache & BIT(BUGGY_LPC_IRQ)))
			generic_handle_irq(irq_find_mapping(gc->domain, BUGGY_LPC_IRQ));
		else
			spurious_interrupt();
	}

	while (pending) {
		int bit = __ffs(pending);

		generic_handle_irq(irq_find_mapping(gc->domain, bit));
		pending &= ~BIT(bit);
	}

	chained_irq_exit(chip, desc);
}

static void map_cache_set_core(struct iointc_priv *priv, int irq, int core)
{
	priv->map_cache[irq] &= ~GENMASK(3, 0);
	priv->map_cache[irq] |= BIT(core);
}

static void write_map_cache(struct iointc_priv *priv, int irq)
{
	writeb(priv->map_cache[irq],
		priv->base + IOINTC_REG_INTx_MAP(irq));
}

static void iointc_set_bit(struct irq_chip_generic *gc,
				unsigned int offset,
				u32 mask, bool set)
{
	if (set)
		writel(readl(gc->reg_base + offset) | mask,
				gc->reg_base + offset);
	else
		writel(readl(gc->reg_base + offset) & ~mask,
				gc->reg_base + offset);
}

static int iointc_set_type(struct irq_data *data, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	u32 mask = data->mask;
	unsigned long flags;

	irq_gc_lock_irqsave(gc, flags);
	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		iointc_set_bit(gc, IOINTC_REG_INTC_EDGE, mask, false);
		iointc_set_bit(gc, IOINTC_REG_INTC_POL, mask, true);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		iointc_set_bit(gc, IOINTC_REG_INTC_EDGE, mask, false);
		iointc_set_bit(gc, IOINTC_REG_INTC_POL, mask, false);
		break;
	case IRQ_TYPE_EDGE_RISING:
		iointc_set_bit(gc, IOINTC_REG_INTC_EDGE, mask, true);
		iointc_set_bit(gc, IOINTC_REG_INTC_POL, mask, true);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		iointc_set_bit(gc, IOINTC_REG_INTC_EDGE, mask, true);
		iointc_set_bit(gc, IOINTC_REG_INTC_POL, mask, false);
		break;
	default:
		return -EINVAL;
	}
	irq_gc_unlock_irqrestore(gc, flags);

	irqd_set_trigger_type(data, type);
	return 0;
}

static int iointc_set_affinity(struct irq_data *idata,
				const cpumask_t *cpu_mask, bool force)
{
	return -ENAVAIL;
}

static void iointc_resume(struct irq_chip_generic *gc)
{
	struct iointc_priv *priv = gc->private;
	unsigned long flags;
	int i;

	irq_gc_lock_irqsave(gc, flags);
	/* Revert map cache */
	for (i = 0; i < IOINTC_CHIP_IRQ; i++)
		write_map_cache(priv, i);

	/* Revert mask cache again */
	writel(gc->mask_cache, gc->reg_base + IOINTC_REG_INTC_DISABLE);
	writel(~gc->mask_cache, gc->reg_base + IOINTC_REG_INTC_ENABLE);
	irq_gc_unlock_irqrestore(gc, flags);
}

static void validate_parent_mask(struct iointc_priv *priv, u32 of_parent_int_map[])
{
	u32 proceed_mask = 0x0, duplicated_mask = 0x0;
	int i;
	int fallback_parent = __ffs(priv->possible_parent_mask);

	for (i = 0; i < IOINTC_NUM_PARENT; i++) {
		/* Try if the parent is avilable */
		if (!(priv->possible_parent_mask & BIT(i)))
			continue;

		priv->handler[i].parent_int_map = of_parent_int_map[i];

		/* Detect if the IRQ have previously proceed */
		duplicated_mask |= (priv->handler[i].parent_int_map & proceed_mask);
		proceed_mask |= priv->handler[i].parent_int_map;
	}

	/* Fallback IRQs with no map bit set */
	while (~proceed_mask) {
		int bit = __ffs(~proceed_mask);

		pr_warn("loongson-iointc: Found homeless IRQ %d, map to INT%d\n",
			bit, fallback_parent);
		priv->handler[fallback_parent].parent_int_map |= BIT(bit);
		proceed_mask |= BIT(bit);
	}

	/* Fallback IRQs with mutiple map bit set */
	while (duplicated_mask) {
		int bit = __ffs(duplicated_mask);

		pr_warn("loongson-iointc: IRQ %d have mutiple parents, map to INT%d\n",
			bit, fallback_parent);
		/* Clear the bit in all parent bits */
		for (i = 0; i < IOINTC_NUM_PARENT; i++)
			priv->handler[i].parent_int_map &= ~BIT(bit);

		priv->handler[fallback_parent].parent_int_map |= BIT(bit);
		duplicated_mask &= ~BIT(bit);
	}

	/* Generate parent INT part of map Cache */
	for (i = 0; i < IOINTC_NUM_PARENT; i++) {
		u32 pending = priv->handler[i].parent_int_map;

		while (pending) {
			int bit = __ffs(pending);

			priv->map_cache[bit] = BIT(i) << IOINTC_SHIFT_INTx;
			pending &= ~BIT(bit);
		}
	}
}

static const char *parent_names[] = {"int0", "int1", "int2", "int3"};

int __init iointc_of_init(struct device_node *node,
				struct device_node *parent)
{
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	struct irq_chip_type *ct;
	struct iointc_priv *priv;
	u32 of_parent_int_map[IOINTC_NUM_PARENT];
	int parent_irq[IOINTC_NUM_PARENT];
	int core = loongson_sysconf.boot_cpu_id;
	int i, err = 0;
	int sz;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = of_iomap(node, 0);
	if (!priv->base) {
		err = -ENODEV;
		goto out_free_priv;
	}

	if (of_device_is_compatible(node, "loongson,iointc-1.0"))
		priv->have_lpc_irq_bug = true;

	for (i = 0; i < IOINTC_NUM_PARENT; i++) {
		parent_irq[i] = of_irq_get_byname(node, parent_names[i]);
		if (parent_irq[i] >= 0)
			priv->possible_parent_mask |= BIT(i);
	}


	if (!priv->possible_parent_mask) {
		pr_err("loongson-iointc: No parent\n");
		err = -ENOMEM;
		goto out_iounmap;
	}

	sz = of_property_read_variable_u32_array(node, "loongson,parent_int_map",
						&of_parent_int_map[0], IOINTC_NUM_PARENT,
						IOINTC_NUM_PARENT);
	if (sz < 4) {
		pr_err("loongson-iointc: No parent_int_map\n");
		err = -ENODEV;
		goto out_iounmap;
	}

	/* Setup IRQ domain */
	domain = irq_domain_add_linear(node, 32,
					&irq_generic_chip_ops, priv);
	if (!domain) {
		pr_err("loongson-iointc: cannot add IRQ domain\n");
		err = -ENOMEM;
		goto out_iounmap;
	}

	err = irq_alloc_domain_generic_chips(domain, 32, 1,
					node->full_name, handle_level_irq,
					IRQ_NOPROBE, 0, 0);
	if (err) {
		pr_err("loongson-iointc: unable to register IRQ domain\n");
		err = -ENOMEM;
		goto out_free_domain;
	}


	/* Disable all IRQs */
	writel(0xffffffff, priv->base + IOINTC_REG_INTC_DISABLE);
	/* Set to level triggered */
	writel(0x0, priv->base + IOINTC_REG_INTC_EDGE);

	validate_parent_mask(priv, &of_parent_int_map[0]);

	for (i = 0; i < IOINTC_CHIP_IRQ; i++) {
		map_cache_set_core(priv, i, core);
		write_map_cache(priv, i);
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->private = priv;
	gc->reg_base = priv->base;
	gc->domain = domain;
	gc->resume = iointc_resume;

	ct = gc->chip_types;
	ct->regs.enable = IOINTC_REG_INTC_ENABLE;
	ct->regs.disable = IOINTC_REG_INTC_DISABLE;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_mask_ack = irq_gc_mask_disable_reg;
	ct->chip.irq_set_type = iointc_set_type;
	ct->chip.irq_set_affinity = iointc_set_affinity;

	gc->mask_cache = 0xffffffff;
	priv->gc = gc;

	for (i = 0; i < IOINTC_NUM_PARENT; i++) {
		if (parent_irq[i] < 0)
			continue;

		priv->handler[i].priv = priv;
		irq_set_chained_handler_and_data(parent_irq[i],
				iointc_chained_handle_irq, &priv->handler[i]);
	}

	return 0;

out_free_domain:
	irq_domain_remove(domain);
out_iounmap:
	iounmap(priv->base);
out_free_priv:
	kfree(priv);

	return err;
}

IRQCHIP_DECLARE(loongson_iointc_1_0, "loongson,iointc-1.0", iointc_of_init);
IRQCHIP_DECLARE(loongson_iointc_1_0a, "loongson,iointc-1.0a", iointc_of_init);
