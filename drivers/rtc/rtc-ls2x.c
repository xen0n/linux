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

#define LS2X_TIMESTAMP_END 454861871999LL /* 16383-12-31T23:59:59Z */

struct ls2x_rtc_priv {
	spinlock_t lock;
	struct rtc_device *rtc_dev;
	void __iomem *rtc_base;
};

static inline u32 ls2x_rtc_read(struct ls2x_rtc_priv *priv, unsigned int addr)
{
	return readl(priv->rtc_base + addr);
}

static inline void ls2x_rtc_write(struct ls2x_rtc_priv *priv,
				  u32 val, unsigned int addr)
{
	writel(val, priv->rtc_base + addr);
}

static int ls2x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ls2x_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned int read0;
	unsigned int read1;

	spin_lock_irq(&priv->lock);
	read1 = ls2x_rtc_read(priv, TOY_READ1_REG);
	read0 = ls2x_rtc_read(priv, TOY_READ0_REG);
	spin_unlock_irq(&priv->lock);

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

	write0 = (tm->tm_sec << TOY_SEC_SHIFT) & TOY_SEC;
	write0 |= (tm->tm_min << TOY_MIN_SHIFT) & TOY_MIN;
	write0 |= (tm->tm_hour << TOY_HOUR_SHIFT) & TOY_HOUR;
	write0 |= (tm->tm_mday << TOY_DAY_SHIFT) & TOY_DAY;
	write0 |= ((tm->tm_mon + 1) << TOY_MON_SHIFT) & TOY_MON;
	write1 = tm->tm_year;

	spin_lock_irq(&priv->lock);
	ls2x_rtc_write(priv, write0, TOY_WRITE0_REG);
	ls2x_rtc_write(priv, write1, TOY_WRITE1_REG);
	spin_unlock_irq(&priv->lock);

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
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (unlikely(!priv))
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	priv->rtc_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->rtc_base)) {
		dev_err(dev, "Failed to map rtc registers\n");
		return PTR_ERR(priv->rtc_base);
	}

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		dev_err(dev, "Failed to allocate rtc device: %d\n", ret);
		return ret;
	}

	rtc->ops = &ls2x_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_0000;
	rtc->range_max = LS2X_TIMESTAMP_END;
	priv->rtc_dev = rtc;

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
