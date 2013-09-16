/*
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/board.h>
#include <mach/gpio.h>

#define FT

#ifdef FT
#define CONSOLE_LOGLEVEL 9
#define ARM_PLL_MHZ (312)
#else
#define CONSOLE_LOGLEVEL 9
#define ARM_PLL_MHZ (816)
#endif

static void __init machine_rk2928_board_init(void)
{
	console_loglevel = CONSOLE_LOGLEVEL;
}

#define ft_printk(fmt, arg...) \
	printk(KERN_EMERG fmt, ##arg)

void __init board_clock_init(void)
{
	rk2928_clock_data_init(periph_pll_default, codec_pll_default, RK30_CLOCKS_DEFAULT_FLAGS);
	clk_set_rate(clk_get(NULL, "cpu"), ARM_PLL_MHZ * 1000 * 1000);
	preset_lpj = loops_per_jiffy;
}

static void __init ft_fixup(struct machine_desc *desc, struct tag *tags,
			char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PLAT_PHYS_OFFSET;
	mi->bank[0].size = SZ_512M;
}

MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= ft_fixup,
	.map_io		= rk2928_map_io,
	.init_irq	= rk2928_init_irq,
	.timer		= &rk2928_timer,
	.init_machine	= machine_rk2928_board_init,
MACHINE_END

