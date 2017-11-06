/*
 * Copyright (C) 2017 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define PMU_MISC2			0x170
#define  PMU_MISC2_REG0_BO_STATUS	0x8
#define  PMU_MISC2_REG1_BO_STATUS	0x800
#define  PMU_MISC2_REG2_BO_STATUS	0x80000

static irqreturn_t anatop_dig_bo_handler(int irq, void *data)
{
	struct regmap *regmap = data;
	u32 val;

	regmap_read(regmap, PMU_MISC2, &val);
	if (!(val & (PMU_MISC2_REG0_BO_STATUS |
	             PMU_MISC2_REG1_BO_STATUS |
		     PMU_MISC2_REG2_BO_STATUS))) {
		pr_warn_ratelimited("brownout detected on unknown regulator\n");
		return IRQ_HANDLED;
	}

	if (val & PMU_MISC2_REG0_BO_STATUS)
		pr_warn_ratelimited("brownout detected on regulator ARM\n");
	if (val & PMU_MISC2_REG1_BO_STATUS)
		pr_warn_ratelimited("brownout detected on regulator PU\n");
	if (val & PMU_MISC2_REG2_BO_STATUS)
		pr_warn_ratelimited("brownout detected on regulator SOC\n");

	return IRQ_HANDLED;
}

static int imx6_anatop_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int irq, ret;

	if (!of_machine_is_compatible("fsl,imx6qp"))
		return 0;

	irq = platform_get_irq(pdev, 2);
	if (irq < 0)
		return irq;

	regmap = syscon_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = devm_request_irq(&pdev->dev, irq, anatop_dig_bo_handler, 0,
			       "anatop dig brownout", regmap);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id imx6_anatop_dt_ids[] = {
	{ .compatible = "fsl,imx6q-anatop" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx6_anatop_dt_ids);

static struct platform_driver imx6_anatop_driver = {
	.driver = {
		.name = "imx6-anatop",
		.of_match_table = imx6_anatop_dt_ids,
	},
	.probe = imx6_anatop_probe,
};
module_platform_driver(imx6_anatop_driver);

MODULE_AUTHOR("Lucas Stach <l.stach@pengutronix.de>");
MODULE_DESCRIPTION("Freescale i.MX6 anatop driver");
MODULE_LICENSE("GPL v2");
