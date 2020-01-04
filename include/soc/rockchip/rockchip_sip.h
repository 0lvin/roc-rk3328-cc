/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Lin Huang <hl@rock-chips.com>
 */
#ifndef __SOC_ROCKCHIP_SIP_H
#define __SOC_ROCKCHIP_SIP_H

#define ROCKCHIP_SIP_DRAM_FREQ			0x82000008
#define ROCKCHIP_SIP_CONFIG_DRAM_INIT		0x00
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_RATE	0x01
#define ROCKCHIP_SIP_CONFIG_DRAM_ROUND_RATE	0x02
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_AT_SR	0x03
#define ROCKCHIP_SIP_CONFIG_DRAM_GET_BW		0x04
#define ROCKCHIP_SIP_CONFIG_DRAM_GET_RATE	0x05
#define ROCKCHIP_SIP_CONFIG_DRAM_CLR_IRQ	0x06
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_PARAM	0x07
#define ROCKCHIP_SIP_CONFIG_DRAM_SET_ODT_PD	0x08
#define ROCKCHIP_SIP_CONFIG_DRAM_GET_VERSION	0x08

#include <linux/arm-smccc.h>
#include <linux/io.h>

/* SMC function IDs for SiP Service queries, compatible with kernel-3.10 */
#define SIP_ACCESS_REG		0x82000002
#define SIP_SUSPEND_MODE	0x82000003
#define SIP_SHARE_MEM		0x82000009
#define SIP_SIP_VERSION		0x8200000a
#define SIP_REMOTECTL_CFG	0x8200000b

/* Rockchip Sip version */
#define SIP_IMPLEMENT_V2                (2)

/* SIP_ACCESS_REG: read or write */
#define SECURE_REG_WR			0x1

/* Error return code */
#define IS_SIP_ERROR(x)			(!!(x))

/* SIP_SUSPEND_MODE32 call types */
#define SUSPEND_MODE_CONFIG		0x01
#define WKUP_SOURCE_CONFIG		0x02
#define PWM_REGULATOR_CONFIG		0x03
#define GPIO_POWER_CONFIG		0x04
#define SUSPEND_DEBUG_ENABLE		0x05
#define APIOS_SUSPEND_CONFIG		0x06
#define VIRTUAL_POWEROFF		0x07

/* SIP_REMOTECTL_CFG call types */
#define	REMOTECTL_SET_IRQ		0xf0
#define REMOTECTL_SET_PWM_CH		0xf1
#define REMOTECTL_SET_PWRKEY		0xf2
#define REMOTECTL_GET_WAKEUP_STATE	0xf3
#define REMOTECTL_ENABLE		0xf4
/* wakeup state */
#define REMOTECTL_PWRKEY_WAKEUP		0xdeadbeaf

/* Share mem page types */
typedef enum {
	SHARE_PAGE_TYPE_INVALID = 0,
	SHARE_PAGE_TYPE_UARTDBG,
	SHARE_PAGE_TYPE_DDR,
	SHARE_PAGE_TYPE_MAX,
} share_page_type_t;

/*
 * Rules: struct arm_smccc_res contains result and data, details:
 *
 * a0: error code(0: success, !0: error);
 * a1~a3: data
 */
#ifdef CONFIG_ROCKCHIP_SIP
struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2);
struct arm_smccc_res sip_smc_request_share_mem(u32 page_num,
					       share_page_type_t page_type);

int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2);
int sip_smc_virtual_poweroff(void);
int sip_smc_remotectl_config(u32 func, u32 data);
#else
static inline struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline struct arm_smccc_res sip_smc_request_share_mem
			(u32 page_num, share_page_type_t page_type)
{
	struct arm_smccc_res tmp = {0};
	return tmp;
}

static inline int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2)
{
	return 0;
}

static inline int sip_smc_virtual_poweroff(void) { return 0; }
static inline int sip_smc_remotectl_config(u32 func, u32 data) { return 0; }
#endif
#endif
