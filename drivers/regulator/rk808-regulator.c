// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator driver for Rockchip RK805/RK808/RK818
 *
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Chris Zhong <zyw@rock-chips.com>
 * Author: Zhang Qing <zhangqing@rock-chips.com>
 *
 * Copyright (C) 2016 PHYTEC Messtechnik GmbH
 *
 * Author: Wadim Egorov <w.egorov@phytec.de>
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/mfd/rk808.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/gpio/consumer.h>

/* Field Definitions */
#define RK808_BUCK_VSEL_MASK	0x3f
#define RK808_BUCK4_VSEL_MASK	0xf
#define RK808_LDO_VSEL_MASK	0x1f

#define RK809_BUCK5_VSEL_MASK		0x7

#define RK817_LDO_VSEL_MASK		0x7f
#define RK817_BOOST_VSEL_MASK		0x7
#define RK817_BUCK_VSEL_MASK		0x7f

#define RK818_BUCK_VSEL_MASK		0x3f
#define RK818_BUCK4_VSEL_MASK		0x1f
#define RK818_LDO_VSEL_MASK		0x1f
#define RK818_LDO3_ON_VSEL_MASK		0xf
#define RK818_BOOST_ON_VSEL_MASK	0xe0

/* Ramp rate definitions for buck1 / buck2 only */
#define RK808_RAMP_RATE_OFFSET		3
#define RK808_RAMP_RATE_MASK		(3 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_2MV_PER_US	(0 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_4MV_PER_US	(1 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_6MV_PER_US	(2 << RK808_RAMP_RATE_OFFSET)
#define RK808_RAMP_RATE_10MV_PER_US	(3 << RK808_RAMP_RATE_OFFSET)

#define RK808_DVS2_POL		BIT(2)
#define RK808_DVS1_POL		BIT(1)

/* Offset from XXX_ON_VSEL to XXX_SLP_VSEL */
#define RK808_SLP_REG_OFFSET 1

/* Offset from XXX_ON_VSEL to XXX_DVS_VSEL */
#define RK808_DVS_REG_OFFSET 2

/* Offset from XXX_EN_REG to SLEEP_SET_OFF_XXX */
#define RK808_SLP_SET_OFF_REG_OFFSET 2

/* max steps for increase voltage of Buck1/2, equal 100mv*/
#define MAX_STEPS_ONE_TIME 8

#define ENABLE_MASK(id)			(BIT(id) | BIT(4 + (id)))
#define DISABLE_VAL(id)			(BIT(4 + (id)))

#define RK817_BOOST_DESC(_id, _match, _supply, _min, _max, _step, _vreg,\
	_vmask, _ereg, _emask, _enval, _disval, _etime, m_drop)		\
	{							\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),	\
		.owner		= THIS_MODULE,				\
		.min_uV		= (_min) * 1000,			\
		.uV_step	= (_step) * 1000,			\
		.vsel_reg	= (_vreg),				\
		.vsel_mask	= (_vmask),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.enable_time	= (_etime),				\
		.min_dropout_uV = (m_drop) * 1000,			\
		.ops		= &rk817_boost_ops,			\
	}

#define RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _enval, _disval, _etime, _ops)		\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),	\
		.owner		= THIS_MODULE,				\
		.min_uV		= (_min) * 1000,			\
		.uV_step	= (_step) * 1000,			\
		.vsel_reg	= (_vreg),				\
		.vsel_mask	= (_vmask),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.enable_time	= (_etime),				\
		.ops		= _ops,			\
	}

#define RK805_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _etime)					\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, 0, 0, _etime, &rk805_reg_ops)

#define RK8XX_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _etime)					\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, 0, 0, _etime, &rk808_reg_ops)

#define RK817_DESC(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _disval, _etime)				\
	RK8XX_DESC_COM(_id, _match, _supply, _min, _max, _step, _vreg,	\
	_vmask, _ereg, _emask, _emask, _disval, _etime, &rk817_reg_ops)

#define RKXX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	_enval, _disval, _ops)						\
	{								\
		.name		= (_match),				\
		.supply_name	= (_supply),				\
		.of_match	= of_match_ptr(_match),			\
		.regulators_node = of_match_ptr("regulators"),		\
		.type		= REGULATOR_VOLTAGE,			\
		.id		= (_id),				\
		.enable_reg	= (_ereg),				\
		.enable_mask	= (_emask),				\
		.enable_val     = (_enval),				\
		.disable_val     = (_disval),				\
		.owner		= THIS_MODULE,				\
		.ops		= _ops					\
	}

#define RK817_DESC_SWITCH(_id, _match, _supply, _ereg, _emask,		\
	_disval)							\
	RKXX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	_emask, _disval, &rk817_switch_ops)

#define RK8XX_DESC_SWITCH(_id, _match, _supply, _ereg, _emask)		\
	RKXX_DESC_SWITCH_COM(_id, _match, _supply, _ereg, _emask,	\
	0, 0, &rk808_switch_ops)

struct rk808_regulator_data {
	struct gpio_desc *dvs_gpio[2];
};

static const int rk808_buck_config_regs[] = {
	RK808_BUCK1_CONFIG_REG,
	RK808_BUCK2_CONFIG_REG,
	RK808_BUCK3_CONFIG_REG,
	RK808_BUCK4_CONFIG_REG,
};

static const struct linear_range rk805_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 59, 12500),	/* 0.7125v - 1.45v */
	REGULATOR_LINEAR_RANGE(1800000, 60, 62, 200000),/* 1.8v - 2.2v */
	REGULATOR_LINEAR_RANGE(2300000, 63, 63, 0),	/* 2.3v - 2.3v */
};

static const struct linear_range rk805_buck4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 26, 100000),	/* 0.8v - 3.4 */
	REGULATOR_LINEAR_RANGE(3500000, 27, 31, 0),	/* 3.5v */
};

static const struct linear_range rk805_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 26, 100000),	/* 0.8v - 3.4 */
};

/* rk818 */
static const struct linear_range rk818_buck_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(712500, 0, 63, 12500),
};

static const struct linear_range rk818_buck4_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 15, 100000),
};

static const struct linear_range rk818_ldo_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0, 16, 100000),
};

static const struct linear_range rk808_ldo3_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 13, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 15, 15, 0),
};

static const struct linear_range rk818_ldo6_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0, 17, 100000),
};

static const struct linear_range rk818_boost_voltage_ranges[] = {
	REGULATOR_LINEAR_RANGE(4700000, 0, 7, 100000),
};

#define RK805_SLP_LDO_EN_OFFSET		-1
#define RK805_SLP_DCDC_EN_OFFSET	2

static int rk818_set_ramp_delay(struct regulator_dev *rdev, int ramp_delay)
{
	struct rk808 *rk818 = rdev->reg_data;
	unsigned int ramp_value = RK808_RAMP_RATE_10MV_PER_US;
	unsigned int reg = rk808_buck_config_regs[rdev->desc->id -
						  RK818_ID_DCDC1];

	switch (rk818->variant) {
	case RK818_ID:
		switch (ramp_delay) {
		case 1 ... 2000:
			ramp_value = RK808_RAMP_RATE_2MV_PER_US;
			break;
		case 2001 ... 4000:
			ramp_value = RK808_RAMP_RATE_4MV_PER_US;
			break;
		case 4001 ... 6000:
			ramp_value = RK808_RAMP_RATE_4MV_PER_US;
			break;
		case 6001 ... 10000:
			break;
		default:
			pr_warn("%s ramp_delay: %d not supported, set 10000\n",
				rdev->desc->name, ramp_delay);
		}
		break;
	case RK805_ID:
		switch (ramp_delay) {
		case 3000:
			ramp_value = RK808_RAMP_RATE_2MV_PER_US;
			break;
		case 6000:
			ramp_value = RK808_RAMP_RATE_4MV_PER_US;
			break;
		case 12500:
			ramp_value = RK808_RAMP_RATE_6MV_PER_US;
			break;
		case 25000:
			ramp_value = RK808_RAMP_RATE_10MV_PER_US;
			break;
		default:
			pr_warn("%s ramp_delay: %d not supported\n",
				rdev->desc->name, ramp_delay);
		}
		break;
	default:
		dev_err(&rdev->dev, "%s: unsupported RK8XX ID %lu\n",
			__func__, rk818->variant);
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, reg,
				  RK808_RAMP_RATE_MASK, ramp_value);
}

static int rk808_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	unsigned int reg;
	int sel = regulator_map_voltage_linear_range(rdev, uv, uv);

	if (sel < 0)
		return -EINVAL;

	reg = rdev->desc->vsel_reg + RK808_SLP_REG_OFFSET;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->vsel_mask,
				  sel);
}

static int rk818_set_suspend_enable(struct regulator_dev *rdev)
{
	unsigned int reg, enable_val;
	int offset = 0;
	struct rk808 *rk818 = rdev->reg_data;

	switch (rk818->variant) {
	case RK818_ID:
		offset = RK808_SLP_SET_OFF_REG_OFFSET;
		enable_val = 0;
		break;
	case RK805_ID:
		if (rdev->desc->id >= RK805_ID_LDO1)
			offset = RK805_SLP_LDO_EN_OFFSET;
		else
			offset = RK805_SLP_DCDC_EN_OFFSET;
		enable_val = rdev->desc->enable_mask;
		break;
	default:
		dev_err(&rdev->dev, "not define sleep en reg offset!!\n");
		return -EINVAL;
	}

	reg = rdev->desc->enable_reg + offset;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  enable_val);
}

static int rk818_set_suspend_disable(struct regulator_dev *rdev)
{
	int offset = 0;
	unsigned int reg, disable_val;
	struct rk808 *rk818 = rdev->reg_data;

	switch (rk818->variant) {
	case RK818_ID:
		offset = RK808_SLP_SET_OFF_REG_OFFSET;
		disable_val = rdev->desc->enable_mask;
		break;
	case RK805_ID:
		if (rdev->desc->id >= RK805_ID_LDO1)
			offset = RK805_SLP_LDO_EN_OFFSET;
		else
			offset = RK805_SLP_DCDC_EN_OFFSET;
		disable_val = 0;
		break;
	default:
		dev_err(&rdev->dev, "not define sleep en reg offset!!\n");
		return -EINVAL;
	}

	reg = rdev->desc->enable_reg + offset;

	return regmap_update_bits(rdev->regmap, reg,
				  rdev->desc->enable_mask,
				  disable_val);
}

static int rk8xx_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int reg;

	reg = rdev->desc->vsel_reg + RK808_SLP_REG_OFFSET;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, reg,
					  FPWM_MODE, FPWM_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, reg, FPWM_MODE, 0);
	default:
		dev_err(&rdev->dev, "do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static int rk8xx_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
					  FPWM_MODE, FPWM_MODE);
	case REGULATOR_MODE_NORMAL:
		return regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
					  FPWM_MODE, 0);
	default:
		dev_err(&rdev->dev, "do not support this mode\n");
		return -EINVAL;
	}

	return 0;
}

static unsigned int rk8xx_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int err;

	err = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (err)
		return err;

	if (val & FPWM_MODE)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static unsigned int rk818_regulator_of_map_mode(unsigned int mode)
{
	if (mode == 1)
		return REGULATOR_MODE_FAST;
	if (mode == 2)
		return REGULATOR_MODE_NORMAL;

	return -EINVAL;
}

static struct regulator_ops rk818_buck1_2_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel	= regulator_set_voltage_time_sel,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_mode		= rk8xx_set_mode,
	.get_mode		= rk8xx_get_mode,
	.set_ramp_delay		= rk818_set_ramp_delay,
	.set_suspend_mode	= rk8xx_set_suspend_mode,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk818_set_suspend_enable,
	.set_suspend_disable	= rk818_set_suspend_disable,
};

static struct regulator_ops rk818_reg_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.set_mode		= rk8xx_set_mode,
	.get_mode		= rk8xx_get_mode,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_mode	= rk8xx_set_suspend_mode,
	.set_suspend_voltage	= rk808_set_suspend_voltage,
	.set_suspend_enable	= rk818_set_suspend_enable,
	.set_suspend_disable	= rk818_set_suspend_disable,
};

static struct regulator_ops rk818_switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_suspend_enable	= rk818_set_suspend_enable,
	.set_suspend_disable	= rk818_set_suspend_disable,
	.set_mode		= rk8xx_set_mode,
	.get_mode		= rk8xx_get_mode,
	.set_suspend_mode	= rk8xx_set_suspend_mode,
};

static const struct regulator_desc rk805_desc[] = {
	{
		.name = "DCDC_REG1",
		.supply_name = "vcc1",
		.id = RK805_ID_DCDC1,
		.ops = &rk818_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_voltage_ranges),
		.vsel_reg = RK805_BUCK1_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(0),
		.disable_val = DISABLE_VAL(0),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG2",
		.supply_name = "vcc2",
		.id = RK805_ID_DCDC2,
		.ops = &rk818_buck1_2_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 64,
		.linear_ranges = rk805_buck_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck_voltage_ranges),
		.vsel_reg = RK805_BUCK2_ON_VSEL_REG,
		.vsel_mask = RK808_BUCK_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(1),
		.disable_val = DISABLE_VAL(1),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG3",
		.supply_name = "vcc3",
		.id = RK805_ID_DCDC3,
		.ops = &rk818_switch_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 1,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(2),
		.disable_val = DISABLE_VAL(2),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "DCDC_REG4",
		.supply_name = "vcc4",
		.id = RK805_ID_DCDC4,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 32,
		.linear_ranges = rk805_buck4_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_buck4_voltage_ranges),
		.vsel_reg = RK805_BUCK4_ON_VSEL_REG,
		.vsel_mask = RK818_BUCK4_VSEL_MASK,
		.enable_reg = RK805_DCDC_EN_REG,
		.enable_mask = ENABLE_MASK(3),
		.disable_val = DISABLE_VAL(3),
		.of_map_mode = rk818_regulator_of_map_mode,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG1",
		.supply_name = "vcc5",
		.id = RK805_ID_LDO1,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 27,
		.linear_ranges = rk805_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_ldo_voltage_ranges),
		.vsel_reg = RK805_LDO1_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK805_LDO_EN_REG,
		.enable_mask = ENABLE_MASK(0),
		.disable_val = DISABLE_VAL(0),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG2",
		.supply_name = "vcc5",
		.id = RK805_ID_LDO2,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 27,
		.linear_ranges = rk805_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_ldo_voltage_ranges),
		.vsel_reg = RK805_LDO2_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK805_LDO_EN_REG,
		.enable_mask = ENABLE_MASK(1),
		.disable_val = DISABLE_VAL(1),
		.enable_time = 400,
		.owner = THIS_MODULE,
	}, {
		.name = "LDO_REG3",
		.supply_name = "vcc6",
		.id = RK805_ID_LDO3,
		.ops = &rk818_reg_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = 27,
		.linear_ranges = rk805_ldo_voltage_ranges,
		.n_linear_ranges = ARRAY_SIZE(rk805_ldo_voltage_ranges),
		.vsel_reg = RK805_LDO3_ON_VSEL_REG,
		.vsel_mask = RK808_LDO_VSEL_MASK,
		.enable_reg = RK805_LDO_EN_REG,
		.enable_mask = ENABLE_MASK(2),
		.disable_val = DISABLE_VAL(2),
		.enable_time = 400,
		.owner = THIS_MODULE,
	},
};

static struct of_regulator_match rk805_reg_matches[] = {
	[RK805_ID_DCDC1] = {
		.name = "RK805_DCDC1",
		.desc = &rk805_desc[RK805_ID_DCDC1] /* for of_map_node */
	},
	[RK805_ID_DCDC2] = {
		.name = "RK805_DCDC2",
		.desc = &rk805_desc[RK805_ID_DCDC2]
	},
	[RK805_ID_DCDC3] = {
		.name = "RK805_DCDC3",
		.desc = &rk805_desc[RK805_ID_DCDC3]
	},
	[RK805_ID_DCDC4] = {
		.name = "RK805_DCDC4",
		.desc = &rk805_desc[RK805_ID_DCDC4]
	},
	[RK805_ID_LDO1]	= { .name = "RK805_LDO1", },
	[RK805_ID_LDO2]	= { .name = "RK805_LDO2", },
	[RK805_ID_LDO3]	= { .name = "RK805_LDO3", },
};

static int rk808_regulator_dt_parse_pdata(struct device *dev,
				   struct device *client_dev,
				   struct regmap *map,
				   struct of_regulator_match *reg_matches,
				   int regulator_nr)
{
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(client_dev->of_node, "regulators");
	if (!np)
		return -ENXIO;

	ret = of_regulator_match(dev, np, reg_matches, regulator_nr);

	of_node_put(np);
	return ret;
}

static int rk808_regulator_probe(struct platform_device *pdev)
{
	struct rk808 *rk808 = dev_get_drvdata(pdev->dev.parent);
	struct i2c_client *client = rk808->i2c;
	struct regulator_config config = {};
	struct regulator_dev *rk808_rdev;
	struct of_regulator_match *reg_matches;
	const struct regulator_desc *regulators;
	int ret, i, nregulators;

	switch (rk808->variant) {
	case RK805_ID:
		regulators = rk805_desc;
		reg_matches = rk805_reg_matches;
		nregulators = RK805_NUM_REGULATORS;
		break;
	default:
		dev_err(&client->dev, "unsupported RK8XX ID %lu\n",
			rk808->variant);
		return -EINVAL;
	}

	ret = rk808_regulator_dt_parse_pdata(&pdev->dev, &client->dev,
					     rk808->regmap, reg_matches, nregulators);
	if (ret < 0)
		return ret;

	/* Instantiate the regulators */
	for (i = 0; i < nregulators; i++) {
		if (!reg_matches[i].init_data ||
		    !reg_matches[i].of_node)
			continue;

		config.driver_data = rk808;
		config.dev = &client->dev;
		config.regmap = rk808->regmap;
		config.of_node = reg_matches[i].of_node;
		config.init_data = reg_matches[i].init_data;
		rk808_rdev = devm_regulator_register(&pdev->dev,
						     &regulators[i], &config);
		if (IS_ERR(rk808_rdev)) {
			dev_err(&client->dev,
				"failed to register %d regulator\n", i);
			return PTR_ERR(rk808_rdev);
		}
	}

	return 0;
}

static struct platform_driver rk808_regulator_driver = {
	.probe = rk808_regulator_probe,
	.driver = {
		.name = "rk808-regulator"
	},
};

module_platform_driver(rk808_regulator_driver);

MODULE_DESCRIPTION("regulator driver for the RK805/RK808/RK818 series PMICs");
MODULE_AUTHOR("Tony xie <tony.xie@rock-chips.com>");
MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_AUTHOR("Wadim Egorov <w.egorov@phytec.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk808-regulator");
