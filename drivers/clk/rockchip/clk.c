/*
 * Copyright (C) 2013 ROCKCHIP, Inc.
 * Author: chenxing <chenxing@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "clk-ops.h"
#include <linux/clk-private.h>
#include <asm/io.h>

static DEFINE_SPINLOCK(clk_lock);

struct rkclk_divmap_table {
	u32		reg_val;
	u32		div_val;
};

struct rkclk_divinfo {
	struct clk_divider *div;
	void __iomem	*addr;
	u32		shift;
	u32		width;
	u32		div_type;
	u32		max_div;
	u32		fixed_div_val;
	u32		clkops_idx;
	const char	*clk_name;
	const char	*parent_name;
	struct clk_div_table		*div_table;
	struct list_head		node;
};

struct rkclk_muxinfo {
	struct clk_mux	*mux;
	void __iomem	*addr;
	u32		shift;
	u32		width;
	u32		parent_num;
	u32		clkops_idx;
	const char	*clk_name;
	const char	**parent_names;
	struct list_head	node;
};

struct rkclk_fracinfo {
	struct clk_hw	hw;
	void __iomem	*addr;
	u32		shift;
	u32		width;
	u32		frac_type;
	u32		clkops_idx;
	const char	*clk_name;
	const char	*parent_name;
	struct list_head	node;
};

struct rkclk_gateinfo {
	struct clk_gate	*gate;
	void __iomem	*addr;
	u32		shift;
	u32		clkops_idx;
	const char	*clk_name;
	const char	*parent_name;
};

struct rkclk_pllinfo {
	struct clk_hw	hw;
	struct clk_ops	*ops;
	void __iomem	*addr;
	u32		width;
	const char	*clk_name;
	const char	*parent_name;
	/*
	 * const char	**clkout_names;
	 */
	struct list_head	node;
};

struct rkclk {
	const char	*clk_name;
	u32		clk_type;
	/*
	 * store nodes creat this rkclk
	 * */
	struct device_node		*np;
	struct rkclk_pllinfo		*pll_info;
	struct rkclk_muxinfo		*mux_info;
	struct rkclk_divinfo		*div_info;
	struct rkclk_fracinfo		*frac_info;
	struct rkclk_gateinfo		*gate_info;
	struct list_head		node;
};

LIST_HEAD(rk_clks);
void __iomem *reg_start = 0;
#define RKCLK_PLL_TYPE	(1 << 0)
#define RKCLK_MUX_TYPE	(1 << 1)
#define RKCLK_DIV_TYPE	(1 << 2)
#define RKCLK_FRAC_TYPE	(1 << 3)
#define RKCLK_GATE_TYPE	(1 << 4)

static int rkclk_init_muxinfo(struct device_node *np,
		struct rkclk_muxinfo *mux, void __iomem *addr)
{
	int cnt, i, ret = 0;
	u8 found = 0;
	struct rkclk *rkclk;

	mux = kzalloc(sizeof(struct rkclk_muxinfo), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;
	/*
	 * Get control bit addr
	 */
	ret = of_property_read_u32_index(np, "rockchip,bits", 0, &mux->shift);
	if (ret != 0)
		return -EINVAL;
	ret = of_property_read_u32(np, "rockchip,clkops-idx", &mux->clkops_idx);
	if (ret != 0)
		mux->clkops_idx = CLKOPS_TABLE_END;

	ret = of_property_read_u32_index(np, "rockchip,bits", 1, &mux->width);
	if (ret != 0)
		return -EINVAL;
	mux->addr = addr;

	ret = of_property_read_string(np, "clock-output-names", &mux->clk_name);
	if (ret != 0)
		return -EINVAL;

	/*
	 * Get parents' cnt
	 */
	cnt = of_count_phandle_with_args(np, "clocks", "#clock-cells");
	if (cnt< 0)
		return -EINVAL;

	mux->parent_num = cnt;
	mux->parent_names = kzalloc(cnt * sizeof(char *), GFP_KERNEL);

	clk_debug("%s: parent cnt = %d\n", __func__, cnt);
	for (i = 0; i < cnt ; i++) {

		mux->parent_names[i] = of_clk_get_parent_name(np, i);
	}

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(mux->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->mux_info != NULL)
				clk_err("%s(%d): This clk(%s) has been used\n",
						__func__, __LINE__, mux->clk_name);
			clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
			found = 1;
			rkclk->mux_info = mux;
			rkclk->clk_type |= RKCLK_MUX_TYPE;
			break;
		}
	}
	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		rkclk->clk_name = mux->clk_name;
		rkclk->mux_info = mux;
		rkclk->clk_type |= RKCLK_MUX_TYPE;
		rkclk->np = np;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

		list_add_tail(&rkclk->node, &rk_clks);
	}
	return 0;
}
static int rkclk_init_divinfo(struct device_node *np,
		struct rkclk_divinfo *div, void __iomem *addr)
{
	int cnt = 0, i = 0, ret = 0;
	struct rkclk *rkclk;
	u8 found = 0;

	div = kzalloc(sizeof(struct rkclk_divinfo), GFP_KERNEL);
	if (!div)
		return -ENOMEM;

	of_property_read_u32_index(np, "rockchip,bits", 0, &div->shift);
	of_property_read_u32_index(np, "rockchip,bits", 1, &div->width);
	div->addr = addr;

	of_property_read_u32(np, "rockchip,div-type", &div->div_type);
	ret = of_property_read_u32(np, "rockchip,clkops-idx", &div->clkops_idx);
	if (ret != 0)
		div->clkops_idx = CLKOPS_TABLE_END;

	cnt = of_property_count_strings(np, "clock-output-names");
	if (cnt <= 0)
		div->clk_name = of_clk_get_parent_name(np, 0);
	else {
		ret = of_property_read_string(np, "clock-output-names", &div->clk_name);
		if (ret != 0)
			return -EINVAL;
		div->parent_name = of_clk_get_parent_name(np, 0);
	}

	switch (div->div_type) {
		case CLK_DIVIDER_PLUS_ONE:
		case CLK_DIVIDER_ONE_BASED:
		case CLK_DIVIDER_POWER_OF_TWO:
			break;
		case CLK_DIVIDER_FIXED:
			of_property_read_u32_index(np, "rockchip,div-relations", 0,
					&div->fixed_div_val);
			clk_debug("%s:%s fixed_div = %d\n", __func__,
					div->clk_name, div->fixed_div_val);
			break;
		case CLK_DIVIDER_USER_DEFINE:
			of_get_property(np, "rockchip,div-relations", &cnt);
			cnt /= 4 * 2;
			div->div_table = kzalloc(cnt * sizeof(struct clk_div_table),
					GFP_KERNEL);

			for (i = 0; i < cnt; i++) {
				of_property_read_u32_index(np, "rockchip,div-relations", i * 2,
						&div->div_table[i].val);
				of_property_read_u32_index(np, "rockchip,div-relations", i * 2 + 1,
						&div->div_table[i].div);
				clk_debug("\tGet div table %d: val=%d, div=%d\n",
						i, div->div_table[i].val,
						div->div_table[i].div);
			}
			break;
		default:
			clk_err("%s: %s: unknown rockchip,div-type, please check dtsi\n",
					__func__, div->clk_name);
			break;
	}

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(div->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->div_info != NULL)
				clk_err("%s(Line %d): This clk(%s) has been used\n",
						__func__, __LINE__, rkclk->clk_name);
			clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
			found = 1;
			rkclk->div_info = div;
			rkclk->clk_type |= RKCLK_DIV_TYPE;
			break;
		}
	}
	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		rkclk->clk_name = div->clk_name;
		rkclk->div_info = div;
		rkclk->clk_type |= RKCLK_DIV_TYPE;
		rkclk->np = np;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

		list_add_tail(&rkclk->node, &rk_clks);
	}
	return 0;


}
static int rkclk_init_fracinfo(struct device_node *np,
		struct rkclk_fracinfo *frac, void __iomem *addr)
{
	struct rkclk *rkclk;
	u8 found = 0;
	int ret = 0;

	frac = kzalloc(sizeof(struct rkclk_fracinfo), GFP_KERNEL);
	if (!frac)
		return -ENOMEM;

	of_property_read_u32_index(np, "rockchip,bits", 0, &frac->shift);
	of_property_read_u32_index(np, "rockchip,bits", 1, &frac->width);
	frac->addr = addr;

	ret = of_property_read_u32(np, "rockchip,clkops-idx", &frac->clkops_idx);
	if (ret != 0)
		frac->clkops_idx = CLKOPS_TABLE_END;

	frac->parent_name = of_clk_get_parent_name(np, 0);
	ret = of_property_read_string(np, "clock-output-names", &frac->clk_name);
	if (ret != 0)
		return -EINVAL;

	found = 0;
	list_for_each_entry(rkclk, &rk_clks, node) {
		if (strcmp(frac->clk_name, rkclk->clk_name) == 0) {
			if (rkclk->frac_info != NULL)
				clk_err("%s(%d): This clk(%s) has been used\n",
						__func__, __LINE__, frac->clk_name);
			clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
			found = 1;
			rkclk->frac_info = frac;
			rkclk->clk_type |= RKCLK_FRAC_TYPE;
			break;
		}
	}
	if (!found) {
		rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
		rkclk->clk_name = frac->clk_name;
		rkclk->frac_info = frac;
		rkclk->clk_type |= RKCLK_FRAC_TYPE;
		rkclk->np = np;
		clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

		list_add_tail(&rkclk->node, &rk_clks);
	}
	return 0;
}

static int __init rkclk_init_selcon(struct device_node *np)
{
	struct device_node *node_con, *node;
	void __iomem *reg = 0;

	struct rkclk_divinfo *divinfo;
	struct rkclk_muxinfo *muxinfo;
	struct rkclk_fracinfo *fracinfo;

	for_each_available_child_of_node(np, node_con) {

		reg = of_iomap(node_con, 0);

		for_each_available_child_of_node(node_con, node) {

			if (of_device_is_compatible(node, "rockchip,rk3188-div-con"))
				rkclk_init_divinfo(node, divinfo, reg);

			else if (of_device_is_compatible(node, "rockchip,rk3188-mux-con"))
				rkclk_init_muxinfo(node, muxinfo, reg);

			else if (of_device_is_compatible(node, "rockchip,rk3188-frac-con"))
				rkclk_init_fracinfo(node, fracinfo, reg);

			else if (of_device_is_compatible(node, "rockchip,rk3188-inv-con"))
				clk_debug("INV clk\n");

			else
				clk_err("%s: unknown controler type, plz check dtsi "
						"or add type support\n", __func__);

		}
	}
	return 0;
}

static int __init rkclk_init_gatecon(struct device_node *np)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node;
	const char *clk_parent;
	const char *clk_name;
	void __iomem *reg;
	void __iomem *reg_idx;
	int cnt;
	int reg_bit;
	int i;
	struct rkclk_gateinfo *gateinfo;
	u8 found = 0;
	struct rkclk *rkclk;

	for_each_available_child_of_node(np, node) {
		cnt = of_property_count_strings(node, "clock-output-names");
		if (cnt < 0) {
			clk_err("%s: error in clock-output-names %d\n",
					__func__, cnt);
			continue;
		}

		if (cnt == 0) {
			pr_info("%s: nothing to do\n", __func__);
			continue;
		}

		reg = of_iomap(node, 0);

		clk_data = kzalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
		if (!clk_data)
			return -ENOMEM;

		clk_data->clks = kzalloc(cnt * sizeof(struct clk *), GFP_KERNEL);
		if (!clk_data->clks) {
			kfree(clk_data);
			return -ENOMEM;
		}

		for (i = 0; i < cnt; i++) {
			of_property_read_string_index(node, "clock-output-names",
					i, &clk_name);

			/* ignore empty slots */
			if (!strcmp("reserved", clk_name))
				continue;

			clk_parent = of_clk_get_parent_name(node, i);

			reg_idx = reg + (4 * (i / 16));
			reg_bit = (i % 16);

			gateinfo = kzalloc(sizeof(struct rkclk_gateinfo), GFP_KERNEL);
			gateinfo->clk_name = clk_name;
			gateinfo->parent_name = clk_parent;
			gateinfo->addr = reg;
			gateinfo->shift = reg_bit;
			found = 0;
			list_for_each_entry(rkclk, &rk_clks, node) {
				if (strcmp(clk_name, rkclk->clk_name) == 0) {
					if (rkclk->gate_info != NULL)
						clk_err("%s(%d): This clk(%s) has been used\n",
								__func__, __LINE__, clk_name);
					clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
					found = 1;
					rkclk->gate_info = gateinfo;
					rkclk->clk_type |= RKCLK_GATE_TYPE;
					break;
				}
			}
			if (!found) {
				rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
				rkclk->clk_name = gateinfo->clk_name;
				rkclk->gate_info = gateinfo;
				rkclk->clk_type |= RKCLK_GATE_TYPE;
				rkclk->np = node;
				clk_debug("%s: creat %s\n", __func__, rkclk->clk_name);

				list_add_tail(&rkclk->node, &rk_clks);
			}
		}

	}
	return 0;
}
static int __init rkclk_init_pllcon(struct device_node *np)
{
	struct rkclk_pllinfo *pllinfo;
	struct device_node *node;
	struct rkclk *rkclk;
	void __iomem	*reg;
	int i = 0;
	int ret = 0, clknum = 0;
	u8 found = 0;

	for_each_available_child_of_node(np, node) {
		clknum = of_property_count_strings(node, "clock-output-names");
		if (clknum < 0) {
			clk_err("%s: error in get clock-output-names numbers = %d\n",
					__func__, clknum);
			return -EINVAL;
		}
		reg = of_iomap(node, 0);
		if (reg_start == 0)
			reg_start = reg;
		for (i = 0; i < clknum; i++) {
			pllinfo = kzalloc(sizeof(struct rkclk_pllinfo), GFP_KERNEL);
			if (!pllinfo)
				return -ENOMEM;

			/*
			 * Get pll parent name
			 */
			pllinfo->parent_name = of_clk_get_parent_name(node, i);

			/*
			 * Get pll output name
			 */
			of_property_read_string_index(node, "clock-output-names",
					i, &pllinfo->clk_name);

			pllinfo->addr = reg;

			ret = of_property_read_u32_index(node, "reg", 1, &pllinfo->width);
			if (ret != 0) {
				clk_err("%s: cat not get reg info\n", __func__);
			}

			clk_debug("%s: parent=%s, pllname=%s, reg =%08x, cnt=%d\n", __func__,
					pllinfo->parent_name, pllinfo->clk_name,
					(u32)pllinfo->addr, pllinfo->width);

			found = 0;
			list_for_each_entry(rkclk, &rk_clks, node) {
				if (strcmp(pllinfo->clk_name, rkclk->clk_name) == 0) {
					if (rkclk->pll_info != NULL)
						clk_err("%s(%d): This clk(%s) has been used\n",
								__func__, __LINE__, pllinfo->clk_name);
					clk_debug("%s: find match %s\n", __func__, rkclk->clk_name);
					found = 1;
					rkclk->pll_info = pllinfo;
					rkclk->clk_type |= RKCLK_PLL_TYPE;
					break;
				}
			}
			if (!found) {
				rkclk = kzalloc(sizeof(struct rkclk), GFP_KERNEL);
				rkclk->clk_name = pllinfo->clk_name;
				rkclk->pll_info = pllinfo;
				rkclk->clk_type |= RKCLK_PLL_TYPE;
				rkclk->np = node;

				list_add_tail(&rkclk->node, &rk_clks);
			}
		}
	}

	return 0;
}

#define MHZ (1000 * 1000)
static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	clk_debug("%s\n", __func__);
	if (strncmp(hw->clk->name, "clk_apll", sizeof("clk_apll")) == 0) {
		return 600 * MHZ;
	} else if (strncmp(hw->clk->name, "clk_dpll", sizeof("clk_dpll")) == 0) {
		return 300 * MHZ;
	}else if (strncmp(hw->clk->name, "clk_cpll", sizeof("clk_cpll")) == 0) {
		return 132 * MHZ;
	}else if (strncmp(hw->clk->name, "clk_gpll", sizeof("clk_gpll")) == 0) {
		return 891 * MHZ;
	} else
		clk_err("Unknown PLL\n");
	return -EINVAL;
}
static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	clk_debug("%s\n", __func__);
	return rate;
}
static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	clk_debug("%s\n", __func__);
	return 0;
}
const struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};
static unsigned long clk_frac_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return parent_rate;
}
static long clk_frac_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}
static int clk_frac_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return 0;
}
const struct clk_ops clk_frac_ops = {
	.recalc_rate = clk_frac_recalc_rate,
	.round_rate = clk_frac_round_rate,
	.set_rate = clk_frac_set_rate,
};
static unsigned long clk_div_special_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return parent_rate;
}
static long clk_div_special_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}
static int clk_div_special_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return 0;
}
// For fixed div clks and For user defined div clk
const struct clk_ops clk_div_special_ops = {
	.recalc_rate = clk_div_special_recalc_rate,
	.round_rate = clk_div_special_round_rate,
	.set_rate = clk_div_special_set_rate,
};
static int rkclk_register(struct rkclk *rkclk)
{
	struct clk_mux		*mux = NULL;
	struct clk_divider	*div = NULL;
	struct clk_gate		*gate = NULL;

	const struct clk_ops	*rate_ops = NULL;
	const struct clk_ops	*mux_ops = NULL;

	struct clk		*clk = NULL;
	const char		**parent_names = NULL;
	struct clk_hw		*rate_hw;
	int			parent_num;
	struct device_node	*node = rkclk->np;
	/* Single clk */
	clk_debug("%s: %s clk_type=%x\n", __func__,
			rkclk->clk_name, rkclk->clk_type);
	if (rkclk->clk_type & RKCLK_PLL_TYPE) {
		div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
		rate_ops = &clk_pll_ops;
		div->reg = rkclk->pll_info->addr;
		div->shift = 0;
		div->width = rkclk->pll_info->width;
		div->flags = CLK_DIVIDER_HIWORD_MASK;
		rate_hw = &div->hw;

		parent_num = 1;
		parent_names = &rkclk->pll_info->parent_name;

	} else if (rkclk->clk_type & RKCLK_FRAC_TYPE) {
		div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
		if (rkclk->frac_info->clkops_idx != CLKOPS_TABLE_END)
			rate_ops = rk_get_clkops(rkclk->frac_info->clkops_idx);
		else
			rate_ops = &clk_frac_ops;
		div->reg = rkclk->frac_info->addr;
		div->shift = (u8)rkclk->frac_info->shift;
		div->width = rkclk->frac_info->width;
		div->flags = CLK_DIVIDER_HIWORD_MASK;
		rate_hw = &div->hw;

		parent_num = 1;
		parent_names = &rkclk->frac_info->parent_name;

	} else if (rkclk->clk_type & RKCLK_DIV_TYPE) {
		div = kzalloc(sizeof(struct clk_divider), GFP_KERNEL);
		if (rkclk->div_info->clkops_idx != CLKOPS_TABLE_END)
			rate_ops = rk_get_clkops(rkclk->div_info->clkops_idx);
		else
			rate_ops = &clk_divider_ops;
		div->reg = rkclk->div_info->addr;
		div->shift = (u8)rkclk->div_info->shift;
		div->width = rkclk->div_info->width;
		div->flags = CLK_DIVIDER_HIWORD_MASK | rkclk->div_info->div_type;
		rate_hw = &div->hw;
		if (rkclk->div_info->div_table)
			div->table = rkclk->div_info->div_table;

		parent_num = 1;
		parent_names = &rkclk->div_info->parent_name;
		if (rkclk->clk_type != (rkclk->clk_type & CLK_DIVIDER_MASK)) {
			// FIXME: fixed div add here
			clk_err("%s: %d, unknown clk_type=%x\n",
					__func__, __LINE__, rkclk->clk_type);

		}
	}

	if (rkclk->clk_type & RKCLK_MUX_TYPE) {
		mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
		mux->reg = rkclk->mux_info->addr;
		mux->shift = (u8)rkclk->mux_info->shift;
		mux->mask = (1 << rkclk->mux_info->width) - 1;
		mux->flags = CLK_MUX_HIWORD_MASK;
		mux_ops = &clk_mux_ops;
		if (rkclk->mux_info->clkops_idx != CLKOPS_TABLE_END) {
			rate_hw = kzalloc(sizeof(struct clk_hw), GFP_KERNEL);
			rate_ops = rk_get_clkops(rkclk->mux_info->clkops_idx);
		}

		parent_num = rkclk->mux_info->parent_num;
		parent_names = rkclk->mux_info->parent_names;
	}

	if (rkclk->clk_type & RKCLK_GATE_TYPE) {
		gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
		gate->reg = rkclk->gate_info->addr;
		gate->bit_idx = rkclk->gate_info->shift;
		gate->flags = CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE;

	}

	// FIXME: flag(CLK_IGNORE_UNUSED) may need an input argument
	if (rkclk->clk_type == RKCLK_MUX_TYPE
			&& rkclk->mux_info->clkops_idx == CLKOPS_TABLE_END) {
		clk = clk_register_mux(NULL, rkclk->clk_name,
				rkclk->mux_info->parent_names,
				(u8)rkclk->mux_info->parent_num,
				CLK_SET_RATE_PARENT,
				mux->reg, mux->shift, mux->mask,
				0, &clk_lock);
	} else if (rkclk->clk_type == RKCLK_DIV_TYPE) {
		clk = clk_register_divider(NULL, rkclk->clk_name,
				rkclk->div_info->parent_name,
				CLK_SET_RATE_PARENT, div->reg, div->shift,
				div->width, div->flags, &clk_lock);
	} else if (rkclk->clk_type == RKCLK_GATE_TYPE) {
		clk = clk_register_gate(NULL, rkclk->clk_name,
				rkclk->gate_info->parent_name,
				CLK_IGNORE_UNUSED, gate->reg,
				gate->bit_idx,
				gate->flags, &clk_lock);
	} else {
		int i = 0;
		clk_debug("%s: composite clk(\"%s\") parents:\n",
				__func__, rkclk->clk_name);

		for (i = 0; i < parent_num; i++) {
			clk_debug("\t\t%s: parent[%d]=%s\n", __func__,
					i, parent_names[i]);
		}
		clk = clk_register_composite(NULL, rkclk->clk_name,
				parent_names, parent_num,
				mux ? &mux->hw : NULL, mux ? mux_ops : NULL,
				rate_hw, rate_ops,
				gate ? &gate->hw : NULL, gate ? &clk_gate_ops : NULL,
				CLK_IGNORE_UNUSED);
	}
	if (clk) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		clk_register_clkdev(clk, rkclk->clk_name, NULL);
	} else {
		clk_err("%s: clk(\"%s\") register clk error\n",
				__func__, rkclk->clk_name);
	}
	return 0;
}

struct test_table {
	const char *name;
	u32 rate;
};
struct test_table t_table[] = {
	{.name = "clk_gpu", .rate = 297000000},
	{.name = "dclk_lcdc0", .rate = 297000000},
	{.name = "clk_i2s", .rate = 11289600},
	{.name = "clk_spdif", .rate = 11289600},
	{.name = "clk_sdmmc", .rate = 50000000},
	{.name = "clk_emmc", .rate = 50000000},
	{.name = "clk_sdio", .rate = 50000000},
	{.name = "clk_uart0", .rate = 12288000},
	{.name = "clk_hsadc", .rate = 12288000},
	{.name = "clk_mac",   .rate = 50000000},
	{.name = "clk_cif0",   .rate = 12000000},
	{.name = "aclk_lcdc0",   .rate = 297000000},
};

#ifdef RKCLK_DEBUG
void rk_clk_test(void)
{
	const char *clk_name;
	struct clk *clk;
	u32 rate = 0;

	u32 i = 0, j = 0;
	for (j = 0; j < ARRAY_SIZE(t_table); j++) {
		clk_name = t_table[j].name;
		rate = t_table[j].rate;

		clk = clk_get(NULL, clk_name);
		if (IS_ERR(clk)) {
			clk_err("%s: clk(\"%s\") \tclk_get error\n",
					__func__, clk_name);
		} else
			clk_debug("%s: clk(\"%s\") \tclk_get success\n",
					__func__, __clk_get_name(clk));

		/* TEST: clk_set_rate */
		if (clk->ops->set_rate) {
			if (0 != clk_set_rate(clk, rate)) {
				clk_err("%s: clk(\"%s\") \tclk_set_rate error\n",
						__func__, clk_name);
			} else {
				clk_debug("%s: clk(\"%s\") \tclk_set_rate success\n",
						__func__, __clk_get_name(clk));
			}
		} else {
			clk_debug("%s: clk(\"%s\") have no set ops\n",
					__func__, clk->name);
		}

		for (i = 0; i * 4 <= 0xf4; i++) {
			if (i % 4 == 0)
				printk("\n%s: \t[0x%08x]: ",
						__func__, 0x20000000 + i * 4);
			printk("%08x ", readl(reg_start + i * 4));
		}
		printk("\n\n");
	}
}
EXPORT_SYMBOL_GPL(rk_clk_test);
#else
void rk_clk_test(void){};
EXPORT_SYMBOL_GPL(rk_clk_test);
#endif
extern void clk_dump_tree(void);
static void __init rkclk_init(struct device_node *np)
{
	struct device_node *node;
	struct rkclk *rkclk;

	for_each_available_child_of_node(np, node) {

		if (!ERR_PTR(of_property_match_string(node,
						"compatible",
						"fixed-clock"))) {
			continue;

		} else if (!ERR_PTR(of_property_match_string(node,
						"compatible",
						"rockchip,rk-pll-cons"))) {
			if (ERR_PTR(rkclk_init_pllcon(node))) {
				clk_err("%s: init pll clk err\n", __func__);
				return ;
			}

		} else if (!ERR_PTR(of_property_match_string(node,
						"compatible",
						"rockchip,rk-sel-cons"))) {
			if (ERR_PTR(rkclk_init_selcon(node))) {
				clk_err("%s: init sel cons err\n", __func__);
				return ;
			}

		} else if (!ERR_PTR(of_property_match_string(node,
						"compatible",
						"rockchip,rk-gate-cons"))) {
			if (ERR_PTR(rkclk_init_gatecon(node))) {
				clk_err("%s: init gate cons err\n", __func__);
				return ;
			}

		} else {
			clk_err("%s: unknown\n", __func__);
		}

	};


#if 0
	list_for_each_entry(rkclk, &rk_clks, node) {
		int i;
		clk_debug("%s: clkname = %s; type=%d\n",
				__func__, rkclk->clk_name,
				rkclk->clk_type);
		if (rkclk->pll_info) {
			clk_debug("\t\tpll: name=%s, parent=%s\n",
					rkclk->pll_info->clk_name,
					rkclk->pll_info->parent_name);
		}
		if (rkclk->mux_info) {
			for (i = 0; i < rkclk->mux_info->parent_num; i++)
				clk_debug("\t\tmux name=%s, parent: %s\n",
						rkclk->mux_info->clk_name,
						rkclk->mux_info->parent_names[i]);
		}
		if (rkclk->div_info) {
			clk_debug("\t\tdiv name=%s\n",
					rkclk->div_info->clk_name);
		}
		if (rkclk->frac_info) {
			clk_debug("\t\tfrac name=%s\n",
					rkclk->frac_info->clk_name);
		}
		if (rkclk->gate_info) {
			clk_debug("\t\tgate name=%s, \taddr=%08x, \tshift=%d\n",
					rkclk->gate_info->clk_name,
					(u32)rkclk->gate_info->addr,
					rkclk->gate_info->shift);
		}
	}
#endif
	list_for_each_entry(rkclk, &rk_clks, node) {
		rkclk_register(rkclk);
	}
	/* check clock parents init */
	list_for_each_entry(rkclk, &rk_clks, node) {

		struct clk *clk;
		int i = 0;
		const char *clk_name = rkclk->clk_name;
		clk = clk_get(NULL, clk_name);
		if (IS_ERR(clk)) {
			clk_err("%s: clk(\"%s\") \tclk_get error\n",
					__func__, clk_name);
			continue;
		} else {
			clk_debug("%s: clk(\"%s\") \tclk_get success\n",
					__func__, __clk_get_name(clk));
		}

		if (clk->parents) {
			for (i = 0; i < clk->num_parents; i++) {
				if (clk->parents[i])
					clk_debug("\t\tclk(\"%s\"): init parent:%s\n",
							clk->name,
							clk->parents[i]->name);
				else {
					clk->parents[i] = clk_get(NULL, clk->parent_names[i]);
					clk_debug("\t\tclk(\"%s\"): init parent:%s\n",
							clk->name,
							clk->parents[i]->name);
				}
			}

		} else {
			clk_debug("\t\tNOT A MUX CLK, parent num=%d\n", clk->num_parents);
		}
	}
}

CLK_OF_DECLARE(rk_clocks,	"rockchip,rk-clock-regs",	rkclk_init);
