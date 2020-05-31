// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson-2H Real Time Clock interface for Linux
 *
 * Author: Shaozong Liu <liushaozong@loongson.cn>
 *         Huacai Chen <chenhc@lemote.com>
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/spinlock.h>

#define TOY_TRIM_REG   0x20
#define TOY_WRITE0_REG 0x24
#define TOY_WRITE1_REG 0x28
#define TOY_READ0_REG  0x2c
#define TOY_READ1_REG  0x30
#define TOY_MATCH0_REG 0x34
#define TOY_MATCH1_REG 0x38
#define TOY_MATCH2_REG 0x3c
#define RTC_CTRL_REG   0x40
#define RTC_TRIM_REG   0x60
#define RTC_WRITE0_REG 0x64
#define RTC_READ0_REG  0x68
#define RTC_MATCH0_REG 0x6c
#define RTC_MATCH1_REG 0x70
#define RTC_MATCH2_REG 0x74

#define TOY_MON        GENMASK(31, 26)
#define TOY_MON_SHIFT  26
#define TOY_DAY        GENMASK(25, 21)
#define TOY_DAY_SHIFT  21
#define TOY_HOUR       GENMASK(20, 16)
#define TOY_HOUR_SHIFT 16
#define TOY_MIN        GENMASK(15, 10)
#define TOY_MIN_SHIFT  10
#define TOY_SEC        GENMASK(9, 4)
#define TOY_SEC_SHIFT  4
#define TOY_MSEC       GENMASK(3, 0)
#define TOY_MSEC_SHIFT 0

/* 18283-12-31T23:59:59Z; 18283 = 1900 + 16383 */
#define LS2X_TIMESTAMP_END 514820102399LL

struct ls2x_rtc_priv {
	struct regmap *regmap;
	spinlock_t lock;
};

static const struct regmap_config ls2x_rtc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static int ls2x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ls2x_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned int read0;
	unsigned int read1;
	int ret;

	spin_lock_irq(&priv->lock);
	ret = regmap_read(priv->regmap, TOY_READ1_REG, &read1);
	ret |= regmap_read(priv->regmap, TOY_READ0_REG, &read0);
	spin_unlock_irq(&priv->lock);

	if (unlikely(ret)) {
		dev_err(dev, "Failed to read time\n");
		return -EIO;
	}

	tm->tm_year = read1;
	tm->tm_sec = (read0 & TOY_SEC) >> TOY_SEC_SHIFT;
	tm->tm_min = (read0 & TOY_MIN) >> TOY_MIN_SHIFT;
	tm->tm_hour = (read0 & TOY_HOUR) >> TOY_HOUR_SHIFT;
	tm->tm_mday = (read0 & TOY_DAY) >> TOY_DAY_SHIFT;
	tm->tm_mon = ((read0 & TOY_MON) >> TOY_MON_SHIFT) - 1;

	return 0;
}

static int ls2x_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ls2x_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned int write0;
	unsigned int write1;
	int ret;

	write0 = (tm->tm_sec << TOY_SEC_SHIFT) & TOY_SEC;
	write0 |= (tm->tm_min << TOY_MIN_SHIFT) & TOY_MIN;
	write0 |= (tm->tm_hour << TOY_HOUR_SHIFT) & TOY_HOUR;
	write0 |= (tm->tm_mday << TOY_DAY_SHIFT) & TOY_DAY;
	write0 |= ((tm->tm_mon + 1) << TOY_MON_SHIFT) & TOY_MON;
	write1 = tm->tm_year;

	spin_lock_irq(&priv->lock);
	ret = regmap_write(priv->regmap, TOY_WRITE0_REG, write0);
	ret |= regmap_write(priv->regmap, TOY_WRITE1_REG, write1);
	spin_unlock_irq(&priv->lock);

	if (unlikely(ret)) {
		dev_err(dev, "Failed to set time\n");
		return -EIO;
	}

	return 0;
}

static struct rtc_class_ops ls2x_rtc_ops = {
	.read_time = ls2x_rtc_read_time,
	.set_time = ls2x_rtc_set_time,
};

static int ls2x_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtc_device *rtc;
	struct ls2x_rtc_priv *priv;
	void __iomem *regs;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (unlikely(!priv))
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs)) {
		dev_err(dev, "Failed to map rtc registers\n");
		return PTR_ERR(regs);
	}

	priv->regmap = devm_regmap_init_mmio(dev, regs,
					     &ls2x_rtc_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		dev_err(dev, "Failed to allocate rtc device: %d\n", ret);
		return ret;
	}

	rtc->ops = &ls2x_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_1900;
	rtc->range_max = LS2X_TIMESTAMP_END;

	return rtc_register_device(rtc);
}

static const struct of_device_id ls2x_rtc_of_match[] = {
	{ .compatible = "loongson,ls2k-rtc" },
	{ .compatible = "loongson,ls7a-rtc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ls2x_rtc_of_match);

static struct platform_driver ls2x_rtc_driver = {
	.probe		= ls2x_rtc_probe,
	.driver		= {
		.name	= "ls2x-rtc",
		.of_match_table = of_match_ptr(ls2x_rtc_of_match),
	},
};

module_platform_driver(ls2x_rtc_driver);

MODULE_DESCRIPTION("LS2X RTC driver");
MODULE_AUTHOR("Liu Shaozong");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ls2x-rtc");
