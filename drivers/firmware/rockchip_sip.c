/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2016, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/rockchip/rockchip_sip.h>
#include <asm/cputype.h>
#ifdef CONFIG_ARM
#include <asm/psci.h>
#endif
#include <asm/smp_plat.h>
#include <uapi/linux/psci.h>
#include <linux/ptrace.h>

#ifdef CONFIG_64BIT
#define PSCI_FN_NATIVE(version, name)	PSCI_##version##_FN64_##name
#else
#define PSCI_FN_NATIVE(version, name)	PSCI_##version##_FN_##name
#endif

#define SIZE_PAGE(n)	((n) << 12)

static struct arm_smccc_res __invoke_sip_fn_smc(unsigned long function_id,
						unsigned long arg0,
						unsigned long arg1,
						unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc(function_id, arg0, arg1, arg2, 0, 0, 0, 0, &res);
	return res;
}

struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2)
{
	return __invoke_sip_fn_smc(SIP_DRAM_CONFIG, arg0, arg1, arg2);
}

struct arm_smccc_res sip_smc_get_atf_version(void)
{
	return __invoke_sip_fn_smc(SIP_ATF_VERSION, 0, 0, 0);
}

struct arm_smccc_res sip_smc_get_sip_version(void)
{
	return __invoke_sip_fn_smc(SIP_SIP_VERSION, 0, 0, 0);
}

int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SUSPEND_MODE, ctrl, config1, config2);
	return res.a0;
}

int sip_smc_virtual_poweroff(void)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_FN_NATIVE(1_0, SYSTEM_SUSPEND), 0, 0, 0);
	return res.a0;
}

int sip_smc_remotectl_config(u32 func, u32 data)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_REMOTECTL_CFG, func, data, 0);

	return res.a0;
}

u32 sip_smc_secure_reg_read(u32 addr_phy)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_ACCESS_REG, 0, addr_phy, SECURE_REG_RD);
	if (res.a0)
		pr_err("%s error: %d, addr phy: 0x%x\n",
		       __func__, (int)res.a0, addr_phy);

	return res.a1;
}

int sip_smc_secure_reg_write(u32 addr_phy, u32 val)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_ACCESS_REG, val, addr_phy, SECURE_REG_WR);
	if (res.a0)
		pr_err("%s error: %d, addr phy: 0x%x\n",
		       __func__, (int)res.a0, addr_phy);

	return res.a0;
}

struct arm_smccc_res sip_smc_request_share_mem(u32 page_num,
					       share_page_type_t page_type)
{
	struct arm_smccc_res res;
	unsigned long share_mem_phy;

	res = __invoke_sip_fn_smc(SIP_SHARE_MEM, page_num, page_type, 0);
	if (IS_SIP_ERROR(res.a0))
		goto error;

	share_mem_phy = res.a1;
	res.a1 = (unsigned long)ioremap(share_mem_phy, SIZE_PAGE(page_num));

error:
	return res;
}

struct arm_smccc_res sip_smc_mcu_el3fiq(u32 arg0, u32 arg1, u32 arg2)
{
	return __invoke_sip_fn_smc(SIP_MCU_EL3FIQ_CFG, arg0, arg1, arg2);
}

struct arm_smccc_res sip_smc_vpu_reset(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_SIP_VPU_RESET, arg0, arg1, arg2);
	return res;
}

#ifdef CONFIG_ARM
/*
 * optee work on kernel 3.10 and 4.4, and we have different sip
 * implement. We should tell optee the current rockchip sip version.
 */
static __init int sip_implement_version_init(void)
{
	struct arm_smccc_res res;

	if (!psci_smp_available())
		return 0;

	res = __invoke_sip_fn_smc(SIP_SIP_VERSION, SIP_IMPLEMENT_V2,
				  SECURE_REG_WR, 0);
	if (IS_SIP_ERROR(res.a0))
		pr_err("%s: set rockchip sip version v2 failed\n", __func__);

	return 0;
}
arch_initcall(sip_implement_version_init);
#endif
