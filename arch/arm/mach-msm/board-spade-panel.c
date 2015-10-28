/* linux/arch/arm/mach-msm/board-spade-panel.c
 *
 * Copyright (c) 2010 HTC.
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

#include <asm/io.h>
#include <asm/mach-types.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include "proc_comm.h"
#include <mach/vreg.h>
#include <mach/panel_id.h>

//////

/* LCD */
#define SPADE_LCD_PCLK               (90)
#define SPADE_LCD_DE                 (91)
#define SPADE_LCD_VSYNC              (92)
#define SPADE_LCD_HSYNC              (93)

#define SPADE_LCD_G0                 (18)
#define SPADE_LCD_G1                 (19)
#define SPADE_LCD_G2                 (94)
#define SPADE_LCD_G3                 (95)
#define SPADE_LCD_G4                 (96)
#define SPADE_LCD_G5                 (97)
#define SPADE_LCD_G6                 (98)
#define SPADE_LCD_G7                 (99)

#define SPADE_LCD_B0                 (20)
#define SPADE_LCD_B1                 (21)
#define SPADE_LCD_B2                 (22)
#define SPADE_LCD_B3                 (100)
#define SPADE_LCD_B4                 (101)
#define SPADE_LCD_B5                 (102)
#define SPADE_LCD_B6                 (103)
#define SPADE_LCD_B7                 (104)

#define SPADE_LCD_R0                 (23)
#define SPADE_LCD_R1                 (24)
#define SPADE_LCD_R2                 (25)
#define SPADE_LCD_R3                 (105)
#define SPADE_LCD_R4                 (106)
#define SPADE_LCD_R5                 (107)
#define SPADE_LCD_R6                 (108)
#define SPADE_LCD_R7                 (109)
#define SPADE_LCD_RSTz               (126)

/* ---- COMMON ---- */
static void config_gpio_table(uint32_t *table, int len)
{
	int n;
	unsigned id;
	for(n = 0; n < len; n++) {
		id = table[n];
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
	}
}

enum msm_mdp_hw_revision {
	MDP_REV_20 = 1,
	MDP_REV_22,
	MDP_REV_30,
	MDP_REV_303,
	MDP_REV_31,
	MDP_REV_40,
	MDP_REV_41,
	MDP_REV_42,
	MDP_REV_43,
	MDP_REV_44,
};

#define MDP_BASE          0xA3F00000
#define PMDH_BASE         0xAD600000
#define EMDH_BASE         0xAD700000
#define TVENC_BASE        0xAD400000

struct msm_panel_common_pdata {
	uintptr_t hw_revision_addr;
	int gpio;
	bool bl_lock;
	spinlock_t bl_spinlock;
	int (*backlight_level)(int level, int max, int min);
	int (*pmic_backlight)(int level);
	int (*rotate_panel)(void);
	int (*backlight) (int level, int mode);
	int (*panel_num)(void);
	void (*panel_config_gpio)(int);
	int (*vga_switch)(int select_vga);
	int *gpio_num;
	u32 mdp_max_clk;
#ifdef CONFIG_MSM_BUS_SCALING
	struct msm_bus_scale_pdata *mdp_bus_scale_table;
#endif
	int mdp_rev;
	u32 ov0_wb_size;  /* overlay0 writeback size */
	u32 ov1_wb_size;  /* overlay1 writeback size */
	u32 mem_hid;
	char cont_splash_enabled;
	u32 splash_screen_addr;
	u32 splash_screen_size;
	char mdp_iommu_split_domain;
};

struct lcdc_platform_data {
	int (*lcdc_gpio_config)(int on);
	int (*lcdc_power_save)(int);
	unsigned int (*lcdc_get_clk)(void);
#ifdef CONFIG_MSM_BUS_SCALING
	struct msm_bus_scale_pdata *bus_scale_table;
#endif
};

struct msm_list_device {
  char *name;
  void *data;
};

static struct platform_device msm_lcdc_device = {
	.name   = "lcdc",
	.id     = 0,
};

static struct resource msm_mdp_resources[] = {
	{
		.name   = "mdp",
		.start  = MDP_BASE,
		.end    = MDP_BASE + 0x000F0000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = INT_MDP,
		.end    = INT_MDP,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device msm_mdp_device = {
	.name   = "mdp",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(msm_mdp_resources),
	.resource       = msm_mdp_resources,
};

static void __init msm_register_device(struct platform_device *pdev, void *data)
{
	int ret;

	pdev->dev.platform_data = data;

	ret = platform_device_register(pdev);
	if (ret)
		dev_err(&pdev->dev,
			  "%s: platform_device_register() failed = %d\n",
			  __func__, ret);
}

void __init msm_fb_register_device(char *name, void *data)
{
	if (!strncmp(name, "mdp", 3))
		msm_register_device(&msm_mdp_device, data);
	/*else if (!strncmp(name, "pmdh", 4))
		msm_register_device(&msm_mddi_device, data);
	else if (!strncmp(name, "emdh", 4))
		msm_register_device(&msm_mddi_ext_device, data);
	else if (!strncmp(name, "ebi2", 4))
		msm_register_device(&msm_ebi2_lcd_device, data);
	else if (!strncmp(name, "tvenc", 5))
		msm_register_device(&msm_tvenc_device, data);*/
	else if (!strncmp(name, "lcdc", 4))
		msm_register_device(&msm_lcdc_device, data);
	/*else if (!strncmp(name, "dtv", 3))
		msm_register_device(&msm_dtv_device, data);
	*/
#ifdef CONFIG_FB_MSM_TVOUT
	else if (!strncmp(name, "tvout_device", 12))
		msm_register_device(&tvout_msm_device, data);
#endif
	else
		printk(KERN_ERR "%s: unknown device! %s\n", __func__, name);
}

void __init msm_fb_add_devices(struct msm_list_device *devices, int len)
{
	int i;
	for (i = 0; i < len; ++i)
		msm_fb_register_device(devices[i].name, devices[i].data);
}
//////

#define DEBUG_LCM

#ifdef DEBUG_LCM
#define LCMDBG(fmt, arg...)     printk("[lcm]%s"fmt, __func__, ##arg)
#else
#define LCMDBG(fmt, arg...)     {}
#endif

#define BRIGHTNESS_DEFAULT_LEVEL        102

enum {
	PANEL_AUO,
	PANEL_SHARP,
	PANEL_UNKNOW
};

extern int panel_type;
static struct vreg *vreg_lcm_1v8, *vreg_lcm_2v8;

#define LCM_GPIO_CFG(gpio, func) \
	PCOM_GPIO_CFG(gpio, func, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_4MA)

static uint32_t display_on_gpio_table[] = {
	LCM_GPIO_CFG(SPADE_LCD_PCLK, 1),
	LCM_GPIO_CFG(SPADE_LCD_DE, 1),
	LCM_GPIO_CFG(SPADE_LCD_VSYNC, 1),
	LCM_GPIO_CFG(SPADE_LCD_HSYNC, 1),

	LCM_GPIO_CFG(SPADE_LCD_R0, 1),
	LCM_GPIO_CFG(SPADE_LCD_R1, 1),
	LCM_GPIO_CFG(SPADE_LCD_R2, 1),
	LCM_GPIO_CFG(SPADE_LCD_R3, 1),
	LCM_GPIO_CFG(SPADE_LCD_R4, 1),
	LCM_GPIO_CFG(SPADE_LCD_R5, 1),
	LCM_GPIO_CFG(SPADE_LCD_R6, 1),
	LCM_GPIO_CFG(SPADE_LCD_R7, 1),

	LCM_GPIO_CFG(SPADE_LCD_G0, 1),
	LCM_GPIO_CFG(SPADE_LCD_G1, 1),
	LCM_GPIO_CFG(SPADE_LCD_G2, 1),
	LCM_GPIO_CFG(SPADE_LCD_G3, 1),
	LCM_GPIO_CFG(SPADE_LCD_G4, 1),
	LCM_GPIO_CFG(SPADE_LCD_G5, 1),
	LCM_GPIO_CFG(SPADE_LCD_G6, 1),
	LCM_GPIO_CFG(SPADE_LCD_G7, 1),

	LCM_GPIO_CFG(SPADE_LCD_B0, 1),
	LCM_GPIO_CFG(SPADE_LCD_B1, 1),
	LCM_GPIO_CFG(SPADE_LCD_B2, 1),
	LCM_GPIO_CFG(SPADE_LCD_B3, 1),
	LCM_GPIO_CFG(SPADE_LCD_B4, 1),
	LCM_GPIO_CFG(SPADE_LCD_B5, 1),
	LCM_GPIO_CFG(SPADE_LCD_B6, 1),
	LCM_GPIO_CFG(SPADE_LCD_B7, 1),
	//LCM_GPIO_CFG(SPADE_LCD_RSTz, 1),
};

static uint32_t display_off_gpio_table[] = {
	LCM_GPIO_CFG(SPADE_LCD_PCLK, 0),
	LCM_GPIO_CFG(SPADE_LCD_DE, 0),
	LCM_GPIO_CFG(SPADE_LCD_VSYNC, 0),
	LCM_GPIO_CFG(SPADE_LCD_HSYNC, 0),

	LCM_GPIO_CFG(SPADE_LCD_R0, 0),
	LCM_GPIO_CFG(SPADE_LCD_R1, 0),
	LCM_GPIO_CFG(SPADE_LCD_R2, 0),
	LCM_GPIO_CFG(SPADE_LCD_R3, 0),
	LCM_GPIO_CFG(SPADE_LCD_R4, 0),
	LCM_GPIO_CFG(SPADE_LCD_R5, 0),
	LCM_GPIO_CFG(SPADE_LCD_R6, 0),
	LCM_GPIO_CFG(SPADE_LCD_R7, 0),

	LCM_GPIO_CFG(SPADE_LCD_G0, 0),
	LCM_GPIO_CFG(SPADE_LCD_G1, 0),
	LCM_GPIO_CFG(SPADE_LCD_G2, 0),
	LCM_GPIO_CFG(SPADE_LCD_G3, 0),
	LCM_GPIO_CFG(SPADE_LCD_G4, 0),
	LCM_GPIO_CFG(SPADE_LCD_G5, 0),
	LCM_GPIO_CFG(SPADE_LCD_G6, 0),
	LCM_GPIO_CFG(SPADE_LCD_G7, 0),

	LCM_GPIO_CFG(SPADE_LCD_B0, 0),
	LCM_GPIO_CFG(SPADE_LCD_B1, 0),
	LCM_GPIO_CFG(SPADE_LCD_B2, 0),
	LCM_GPIO_CFG(SPADE_LCD_B3, 0),
	LCM_GPIO_CFG(SPADE_LCD_B4, 0),
	LCM_GPIO_CFG(SPADE_LCD_B5, 0),
	LCM_GPIO_CFG(SPADE_LCD_B6, 0),
	LCM_GPIO_CFG(SPADE_LCD_B7, 0),
};

static void spade_sharp_panel_power(bool on_off)
{
  if (!!on_off) {
    LCMDBG("(%d):\n", on_off);
    config_gpio_table( display_on_gpio_table,
                       ARRAY_SIZE(display_on_gpio_table));
    gpio_set_value(SPADE_LCD_RSTz, 0);
    vreg_enable(vreg_lcm_2v8);
    vreg_enable(vreg_lcm_1v8);
    udelay(10);
    gpio_set_value(SPADE_LCD_RSTz, 1);
    msleep(20);
  } else {
    LCMDBG("(%d):\n", on_off);
    gpio_set_value(SPADE_LCD_RSTz, 0);
    msleep(70);
    vreg_disable(vreg_lcm_2v8);
    vreg_disable(vreg_lcm_1v8);
    config_gpio_table(display_off_gpio_table,
                      ARRAY_SIZE(display_off_gpio_table));
  }
}

static void spade_auo_panel_power(bool on_off)
{
  if (!!on_off) {
    LCMDBG("(%d):\n", on_off);
    gpio_set_value(SPADE_LCD_RSTz, 1);
    udelay(500);
    gpio_set_value(SPADE_LCD_RSTz, 0);
    udelay(500);
    gpio_set_value(SPADE_LCD_RSTz, 1);
    msleep(20);
    config_gpio_table( display_on_gpio_table,
                       ARRAY_SIZE(display_on_gpio_table));
  } else {
    LCMDBG("%s(%d):\n", __func__, on_off);
    gpio_set_value(SPADE_LCD_RSTz, 1);
    msleep(70);
    config_gpio_table( display_off_gpio_table,
                       ARRAY_SIZE(display_off_gpio_table));
  }
}

static int panel_power(int on)
{
  switch (panel_type) {
  case PANEL_AUO:
  case PANEL_ID_SPADE_AUO_N90:
    spade_auo_panel_power(on == 1 ? true : false);
    return 0;
    break;
  case PANEL_SHARP:
  case PANEL_ID_SPADE_SHA_N90:
    spade_sharp_panel_power(on == 1 ? true : false);
    return 0;
    break;
  }
  return -EINVAL;
}

int device_fb_detect_panel(const char *name)
{
  if (!strcmp(name, "lcdc_spade_wvga")) {
    return 0;
  }
  return 0;
}

/* a hacky interface to control the panel power */
static void lcdc_config_gpios(int on)
{
	printk(KERN_INFO "%s: power goes to %d\n", __func__, on);

	if (panel_power(on))
		printk(KERN_ERR "%s: panel_power failed!\n", __func__);
}

static struct msm_panel_common_pdata lcdc_panel_data = {
	.panel_config_gpio = lcdc_config_gpios,
};

static struct platform_device lcdc_spadewvga_panel_device = {
	.name   = "lcdc_spade_wvga",
	.id     = 0,
	.dev    = {
		.platform_data = &lcdc_panel_data,
	}
};

static struct msm_panel_common_pdata mdp_pdata = {
	.hw_revision_addr = 0xac001270,
	.gpio = 30,
	.mdp_max_clk = 192000000,
	.mdp_rev = MDP_REV_40,
};

static int lcdc_panel_power(int on)
{
	int flag_on = !!on;
	static int lcdc_power_save_on;

	if (lcdc_power_save_on == flag_on)
		return 0;

	lcdc_power_save_on = flag_on;

	return panel_power(on);
}

static struct lcdc_platform_data lcdc_pdata = {
	.lcdc_power_save = lcdc_panel_power,
};

struct msm_list_device spade_fb_devices[] = {
  { "mdp", &mdp_pdata },
  { "lcdc", &lcdc_pdata }
};

static int panel_init_power(void)
{
  vreg_lcm_1v8 = vreg_get(0, "gp13");
  if (IS_ERR(vreg_lcm_1v8))
    return PTR_ERR(vreg_lcm_1v8);
  vreg_lcm_2v8 = vreg_get(0, "wlan2");
  if (IS_ERR(vreg_lcm_2v8))
    return PTR_ERR(vreg_lcm_2v8);
  return 0;
}

int __init spade_init_panel(void)
{
  int ret;

  ret = panel_init_power();
  if (ret)
    return ret;

  msm_fb_add_devices(
                     spade_fb_devices, ARRAY_SIZE(spade_fb_devices));

  ret = platform_device_register(&lcdc_spadewvga_panel_device);
  if (ret != 0)
    return ret;

  return 0;
}

device_initcall(spade_init_panel);
