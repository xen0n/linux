// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson-2K/7A RTC driver
 *
 * Based on the original out-of-tree Loongson-2H RTC driver for Linux 2.6.32,
 * by Shaozong Liu <liushaozong@loongson.cn>.
 *
 * Maintained out-of-tree by Huacai Chen <chenhuacai@kernel.org>.
 *
 * Rewritten for mainline by WANG Xuerui <git@xen0n.name>.
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

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
#define TOY_DAY        GENMASK(25, 21)
#define TOY_HOUR       GENMASK(20, 16)
#define TOY_MIN        GENMASK(15, 10)
#define TOY_SEC        GENMASK(9, 4)
#define TOY_MSEC       GENMASK(3, 0)

struct ls2x_rtc_priv {
	struct regmap *regmap;
};

static const struct regmap_config ls2x_rtc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

struct ls2x_rtc_regs {
	u32 reg0;
	u32 reg1;
};

static inline void ls2x_rtc_regs_to_time(struct ls2x_rtc_regs *regs,
					 struct rtc_time *tm)
{
	tm->tm_year = regs->reg1;
	tm->tm_sec = FIELD_GET(TOY_SEC, regs->reg0);
	tm->tm_min = FIELD_GET(TOY_MIN, regs->reg0);
	tm->tm_hour = FIELD_GET(TOY_HOUR, regs->reg0);
	tm->tm_mday = FIELD_GET(TOY_DAY, regs->reg0);
	tm->tm_mon = FIELD_GET(TOY_MON, regs->reg0) - 1;
}

static inline void ls2x_rtc_time_to_regs(struct rtc_time *tm,
					 struct ls2x_rtc_regs *regs)
{
	regs->reg0 = FIELD_PREP(TOY_SEC, tm->tm_sec);
	regs->reg0 |= FIELD_PREP(TOY_MIN, tm->tm_min);
	regs->reg0 |= FIELD_PREP(TOY_HOUR, tm->tm_hour);
	regs->reg0 |= FIELD_PREP(TOY_DAY, tm->tm_mday);
	regs->reg0 |= FIELD_PREP(TOY_MON, tm->tm_mon + 1);
	regs->reg1 = tm->tm_year;
}

static int ls2x_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ls2x_rtc_priv *priv = dev_get_drvdata(dev);
	struct ls2x_rtc_regs regs;
	int ret;

	ret = regmap_read(priv->regmap, TOY_READ1_REG, &regs.reg1);
	if (unlikely(ret))
		return ret;

	ret = regmap_read(priv->regmap, TOY_READ0_REG, &regs.reg0);
	if (unlikely(ret))
		return ret;

	ls2x_rtc_regs_to_time(&regs, tm);

	return 0;
}

static int ls2x_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ls2x_rtc_priv *priv = dev_get_drvdata(dev);
	struct ls2x_rtc_regs regs;
	int ret;

	ls2x_rtc_time_to_regs(tm, &regs);

	ret = regmap_write(priv->regmap, TOY_WRITE0_REG, regs.reg0);
	if (unlikely(ret))
		return ret;

	return regmap_write(priv->regmap, TOY_WRITE1_REG, regs.reg1);
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

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (unlikely(!priv))
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->regmap = devm_regmap_init_mmio(dev, regs,
					     &ls2x_rtc_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &ls2x_rtc_ops;

	/* Due to hardware erratum, all years multiple of 4 are considered
	 * leap year, so only years 2000 through 2099 are usable.
	 *
	 * Previous out-of-tree versions of this driver wrote tm_year directly
	 * into the year register, so epoch 2000 must be used to preserve
	 * semantics on shipped systems.
	 */
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;

	return devm_rtc_register_device(rtc);
}

static const struct of_device_id ls2x_rtc_of_match[] = {
	{ .compatible = "loongson,ls2x-rtc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ls2x_rtc_of_match);

static struct platform_driver ls2x_rtc_driver = {
	.probe		= ls2x_rtc_probe,
	.driver		= {
		.name	= "ls2x-rtc",
		.of_match_table = ls2x_rtc_of_match,
	},
};

module_platform_driver(ls2x_rtc_driver);

MODULE_DESCRIPTION("LS2X RTC driver");
MODULE_AUTHOR("WANG Xuerui");
MODULE_AUTHOR("Huacai Chen");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ls2x-rtc");
