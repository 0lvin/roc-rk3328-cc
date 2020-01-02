/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <linux/clk.h>
#include <linux/nvmem-consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <soc/rockchip/rockchip_opp_select.h>

#define LEAKAGE_TABLE_END	~1
#define LEAKAGE_INVALID		0xff

struct volt_sel_table {
	int min;
	int max;
	int sel;
};

struct pvtm_config {
	unsigned int freq;
	unsigned int volt;
	unsigned int ch[2];
	unsigned int sample_time;
	unsigned int num;
	unsigned int err;
	unsigned int ref_temp;
	int temp_prop[2];
	const char *tz_name;
	struct thermal_zone_device *tz;
};

static int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
				    int *value)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, porp_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == LEAKAGE_INVALID)
		return -EINVAL;

	*value = buf[0];

	kfree(buf);

	return 0;
}

static int rockchip_get_volt_sel_table(struct device_node *np, char *porp_name,
				       struct volt_sel_table **table)
{
	struct volt_sel_table *sel_table;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	sel_table = kzalloc(sizeof(*sel_table) * (count / 3 + 1), GFP_KERNEL);
	if (!sel_table)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i,
					   &sel_table[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &sel_table[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &sel_table[i].sel);
	}
	sel_table[i].min = 0;
	sel_table[i].max = 0;
	sel_table[i].sel = LEAKAGE_TABLE_END;

	*table = sel_table;

	return 0;
}

static int rockchip_get_volt_sel(struct device_node *np, char *name,
				 int value, int *sel)
{
	struct volt_sel_table *table;
	int i, j = -1, ret;

	ret = rockchip_get_volt_sel_table(np, name, &table);
	if (ret)
		return -EINVAL;

	for (i = 0; table[i].sel != LEAKAGE_TABLE_END; i++) {
		if (value >= table[i].min)
			j = i;
	}
	if (j != -1)
		*sel = table[j].sel;
	else
		ret = -EINVAL;

	kfree(table);

	return ret;
}

int rockchip_of_get_lkg_volt_sel(struct device *dev, char *name)
{
	struct device_node *np;
	int leakage, volt_sel;
	int ret;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	ret = rockchip_get_efuse_value(np, name, &leakage);
	if (!ret) {
		dev_info(dev, "%s=%d\n", name, leakage);
		ret = rockchip_get_volt_sel(np, "rockchip,leakage-voltage-sel",
					    leakage, &volt_sel);
		if (!ret) {
			dev_info(dev, "%s-volt-sel=%d\n", name, volt_sel);
			return volt_sel;
		}
	}

	return ret;
}
EXPORT_SYMBOL(rockchip_of_get_lkg_volt_sel);
