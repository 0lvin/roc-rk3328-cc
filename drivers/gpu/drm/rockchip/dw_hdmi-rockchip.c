// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <linux/clk-provider.h>
#include <linux/rockchip/cpu.h>
#include <linux/pm_runtime.h>
#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define RK3228_GRF_SOC_CON2		0x0408
#define RK3228_HDMI_SDAIN_MSK		BIT(14)
#define RK3228_HDMI_SCLIN_MSK		BIT(13)
#define RK3228_GRF_SOC_CON6		0x0418
#define RK3228_HDMI_HPD_VSEL		BIT(6)
#define RK3228_HDMI_SDA_VSEL		BIT(5)
#define RK3228_HDMI_SCL_VSEL		BIT(4)

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3328_GRF_SOC_CON2		0x0408

#define RK3328_HDMI_SDAIN_MSK		BIT(11)
#define RK3328_HDMI_SCLIN_MSK		BIT(10)
#define RK3328_HDMI_HPD_IOE		BIT(2)
#define RK3328_GRF_SOC_CON3		0x040c
/* need to be unset if hdmi or i2c should control voltage */
#define RK3328_HDMI_SDA5V_GRF		BIT(15)
#define RK3328_HDMI_SCL5V_GRF		BIT(14)
#define RK3328_HDMI_HPD5V_GRF		BIT(13)
#define RK3328_HDMI_CEC5V_GRF		BIT(12)
#define RK3328_GRF_SOC_CON4		0x0410
#define RK3328_HDMI_HPD_SARADC		BIT(13)
#define RK3328_HDMI_CEC_5V		BIT(11)
#define RK3328_HDMI_SDA_5V		BIT(10)
#define RK3328_HDMI_SCL_5V		BIT(9)
#define RK3328_HDMI_HPD_5V		BIT(8)

#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

/**
 * struct rockchip_hdmi_chip_data - splite the grf setting of kind of chips
 * @lcdsel_grf_reg: grf register offset of lcdc select
 * @lcdsel_big: reg value of selecting vop big for HDMI
 * @lcdsel_lit: reg value of selecting vop little for HDMI
 */
struct rockchip_hdmi_chip_data {
	int	lcdsel_grf_reg;
	u32	lcdsel_big;
	u32	lcdsel_lit;
};

struct rockchip_hdmi {
	struct device *dev;
	struct regmap *regmap;
	struct drm_encoder encoder;
	const struct rockchip_hdmi_chip_data *chip_data;
	struct clk *vpll_clk;
	struct clk *grf_clk;
	struct dw_hdmi *hdmi;
	struct phy *phy;
	struct clk *hclk_vio;
	struct clk *dclk;

	unsigned int phy_bus_width;
};

#define to_rockchip_hdmi(x)	container_of(x, struct rockchip_hdmi, x)

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg[] = {
	{
		27000000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		36000000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		40000000, {
			{ 0x00b3, 0x0000},
			{ 0x2153, 0x0000},
			{ 0x40f3, 0x0000}
		},
	}, {
		54000000, {
			{ 0x0072, 0x0001},
			{ 0x2142, 0x0001},
			{ 0x40a2, 0x0001},
		},
	}, {
		65000000, {
			{ 0x0072, 0x0001},
			{ 0x2142, 0x0001},
			{ 0x40a2, 0x0001},
		},
	}, {
		66000000, {
			{ 0x013e, 0x0003},
			{ 0x217e, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		74250000, {
			{ 0x0072, 0x0001},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		83500000, {
			{ 0x0072, 0x0001},
		},
	}, {
		108000000, {
			{ 0x0051, 0x0002},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		106500000, {
			{ 0x0051, 0x0002},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		146250000, {
			{ 0x0051, 0x0002},
			{ 0x2145, 0x0002},
			{ 0x4061, 0x0002}
		},
	}, {
		148500000, {
			{ 0x0051, 0x0003},
			{ 0x214c, 0x0003},
			{ 0x4064, 0x0003}
		},
	}, {
		~0UL, {
			{ 0x00a0, 0x000a },
			{ 0x2001, 0x000f },
			{ 0x4002, 0x000f },
		},
	}
};

static const struct dw_hdmi_curr_ctrl rockchip_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		40000000,  { 0x0018, 0x0018, 0x0018 },
	}, {
		65000000,  { 0x0028, 0x0028, 0x0028 },
	}, {
		66000000,  { 0x0038, 0x0038, 0x0038 },
	}, {
		74250000,  { 0x0028, 0x0038, 0x0038 },
	}, {
		83500000,  { 0x0028, 0x0038, 0x0038 },
	}, {
		146250000, { 0x0038, 0x0038, 0x0038 },
	}, {
		148500000, { 0x0000, 0x0038, 0x0038 },
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000},
	}
};

static const struct dw_hdmi_phy_config rockchip_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 148500000, 0x802b, 0x0004, 0x028d},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ 594000000, 0x8039, 0x0000, 0x019d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static int rockchip_hdmi_parse_dt(struct rockchip_hdmi *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;
	int ret;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->vpll_clk = devm_clk_get(hdmi->dev, "vpll");
	if (PTR_ERR(hdmi->vpll_clk) == -ENOENT) {
		hdmi->vpll_clk = NULL;
	} else if (PTR_ERR(hdmi->vpll_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->vpll_clk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->vpll_clk);
	}

	hdmi->grf_clk = devm_clk_get(hdmi->dev, "grf");
	if (PTR_ERR(hdmi->grf_clk) == -ENOENT) {
		hdmi->grf_clk = NULL;
	} else if (PTR_ERR(hdmi->grf_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->grf_clk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->grf_clk);
	}

	hdmi->hclk_vio = devm_clk_get(hdmi->dev, "hclk_vio");
	if (PTR_ERR(hdmi->hclk_vio) == -ENOENT) {
		hdmi->hclk_vio = NULL;
	} else if (PTR_ERR(hdmi->hclk_vio) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hclk_vio)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get hclk_vio clock\n");
		return PTR_ERR(hdmi->hclk_vio);
	}

	hdmi->dclk = devm_clk_get(hdmi->dev, "dclk");
	if (PTR_ERR(hdmi->dclk) == -ENOENT) {
		hdmi->dclk = NULL;
	} else if (PTR_ERR(hdmi->dclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->dclk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get dclk\n");
		return PTR_ERR(hdmi->dclk);
	}

	ret = clk_prepare_enable(hdmi->hclk_vio);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Failed to eanble HDMI hclk_vio: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static enum drm_mode_status
dw_hdmi_rockchip_mode_valid(struct drm_connector *connector,
			    const struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = connector->encoder;
	enum drm_mode_status status = MODE_OK;
	struct drm_device *dev = connector->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct rockchip_hdmi *hdmi;

	/*
	 * Pixel clocks we support are always < 2GHz and so fit in an
	 * int.  We should make sure source rate does too so we don't get
	 * overflow when we multiply by 1000.
	 */
	if (mode->clock > INT_MAX / 1000)
		return MODE_BAD;
	/*
	 * If sink max TMDS clock < 340MHz, we should check the mode pixel
	 * clock > 340MHz is YCbCr420 or not.
	 */
	if (mode->clock > 340000 &&
	    connector->display_info.max_tmds_clock < 340000 &&
	    !drm_mode_is_420(&connector->display_info, mode))
		return MODE_BAD;

	if (!encoder) {
		const struct drm_connector_helper_funcs *funcs;

		funcs = connector->helper_private;
		if (funcs->atomic_best_encoder)
			encoder = funcs->atomic_best_encoder(connector,
							     connector->state);
		else if (funcs->best_encoder)
			encoder = funcs->best_encoder(connector);
		else {
			dump_stack();
			encoder = drm_encoder_find(connector->dev, NULL, connector->encoder_ids[0]);
		}
	}

	if (!encoder || !encoder->possible_crtcs)
		return MODE_BAD;

	hdmi = to_rockchip_hdmi(encoder);

	/*
	 * ensure all drm display mode can work, if someone want support more
	 * resolutions, please limit the possible_crtc, only connect to
	 * needed crtc.
	 */
	drm_for_each_crtc(crtc, connector->dev) {
		int pipe = drm_crtc_index(crtc);
		const struct rockchip_crtc_funcs *funcs =
						priv->crtc_funcs[pipe];

		if (!(encoder->possible_crtcs & drm_crtc_mask(crtc)))
			continue;
		if (!funcs || !funcs->mode_valid)
			continue;

		status = funcs->mode_valid(crtc, mode,
					   DRM_MODE_CONNECTOR_HDMIA);
		if (status != MODE_OK)
			return status;
	}

	return status;
}

static const struct drm_encoder_funcs dw_hdmi_rockchip_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);

	/*
	 * when plug out hdmi it will be switch cvbs and then phy bus width
	 * must be set as 8
	 */
	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, 8);
	clk_disable_unprepare(hdmi->dclk);
}

static bool
dw_hdmi_rockchip_encoder_mode_fixup(struct drm_encoder *encoder,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adj_mode)
{
	return true;
}

static void dw_hdmi_rockchip_encoder_mode_set(struct drm_encoder *encoder,
					      struct drm_display_mode *mode,
					      struct drm_display_mode *adj_mode)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);

	clk_set_rate(hdmi->vpll_clk, adj_mode->clock * 1000);
}

static void dw_hdmi_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;

	if (WARN_ON(!crtc || !crtc->state))
		return;

	if (hdmi->chip_data->lcdsel_grf_reg < 0)
		return;

	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, hdmi->phy_bus_width);

	clk_set_rate(hdmi->dclk, crtc->state->adjusted_mode.crtc_clock * 1000);
	clk_prepare_enable(hdmi->dclk);
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.mode_fixup = dw_hdmi_rockchip_encoder_mode_fixup,
	.mode_set   = dw_hdmi_rockchip_encoder_mode_set,
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
};

static int dw_hdmi_rockchip_genphy_init(struct dw_hdmi *dw_hdmi, void *data,
			     struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	while (hdmi->phy->power_count > 0)
		phy_power_off(hdmi->phy);
	dw_hdmi_set_high_tmds_clock_ratio(dw_hdmi);
	return phy_power_on(hdmi->phy);
}

static void dw_hdmi_rockchip_genphy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	phy_power_off(hdmi->phy);
}

static void dw_hdmi_rk3228_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	regmap_write(hdmi->regmap,
		RK3228_GRF_SOC_CON6,
		HIWORD_UPDATE(RK3228_HDMI_HPD_VSEL | RK3228_HDMI_SDA_VSEL |
			      RK3228_HDMI_SCL_VSEL,
			      RK3228_HDMI_HPD_VSEL | RK3228_HDMI_SDA_VSEL |
			      RK3228_HDMI_SCL_VSEL));

	regmap_write(hdmi->regmap,
		RK3228_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3228_HDMI_SDAIN_MSK | RK3228_HDMI_SCLIN_MSK,
			      RK3228_HDMI_SDAIN_MSK | RK3228_HDMI_SCLIN_MSK));
}

static enum drm_connector_status
dw_hdmi_rk3328_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	enum drm_connector_status status;

	status = dw_hdmi_phy_read_hpd(dw_hdmi, data);

	if (status == connector_status_connected)
		regmap_write(hdmi->regmap,
			RK3328_GRF_SOC_CON4,
			HIWORD_UPDATE(RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V,
				      RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V));
	else
		regmap_write(hdmi->regmap,
			RK3328_GRF_SOC_CON4,
			HIWORD_UPDATE(0, RK3328_HDMI_SDA_5V |
					 RK3328_HDMI_SCL_5V));
	return status;
}

static int inno_dw_hdmi_init(struct rockchip_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return -EPROBE_DEFER;
	}
	/* Enable and map pins to 3V grf-controlled io-voltage */
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON4,
		HIWORD_UPDATE(0, RK3328_HDMI_HPD_SARADC | RK3328_HDMI_CEC_5V |
				 RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V |
				 RK3328_HDMI_HPD_5V));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON3,
		HIWORD_UPDATE(0, RK3328_HDMI_SDA5V_GRF | RK3328_HDMI_SCL5V_GRF |
				 RK3328_HDMI_HPD5V_GRF |
				 RK3328_HDMI_CEC5V_GRF));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK,
			      RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK |
			      RK3328_HDMI_HPD_IOE));
	clk_disable_unprepare(hdmi->grf_clk);
	return 0;
}

static void dw_hdmi_rk3328_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	/* Enable and map pins to 3V grf-controlled io-voltage */
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON4,
		HIWORD_UPDATE(0, RK3328_HDMI_HPD_SARADC | RK3328_HDMI_CEC_5V |
				 RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V |
				 RK3328_HDMI_HPD_5V));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON3,
		HIWORD_UPDATE(0, RK3328_HDMI_SDA5V_GRF | RK3328_HDMI_SCL5V_GRF |
				 RK3328_HDMI_HPD5V_GRF |
				 RK3328_HDMI_CEC5V_GRF));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK,
			      RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK |
			      RK3328_HDMI_HPD_IOE));
}

static const struct dw_hdmi_phy_ops rk3228_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_rk3228_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3228_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3228_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3228_chip_data,
	.phy_ops = &rk3228_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
};

static struct rockchip_hdmi_chip_data rk3288_chip_data = {
	.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
	.lcdsel_big = HIWORD_UPDATE(0, RK3288_HDMI_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3288_HDMI_LCDC_SEL, RK3288_HDMI_LCDC_SEL),
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3288_chip_data,
};

static const struct dw_hdmi_phy_ops rk3328_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_rk3328_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_rk3328_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3328_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3328_chip_data,
	.phy_ops = &rk3328_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.use_drm_infoframe = true,
};

static struct rockchip_hdmi_chip_data rk3399_chip_data = {
	.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
	.lcdsel_big = HIWORD_UPDATE(0, RK3399_HDMI_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3399_HDMI_LCDC_SEL, RK3399_HDMI_LCDC_SEL),
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3399_chip_data,
	.use_drm_infoframe = true,
};

static const struct of_device_id dw_hdmi_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3228-dw-hdmi",
	  .data = &rk3228_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3328-dw-hdmi",
	  .data = &rk3328_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3399-dw-hdmi",
	  .data = &rk3399_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rockchip_dt_ids);

static int dw_hdmi_rockchip_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	const struct of_device_id *match;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct rockchip_hdmi *hdmi;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	match = of_match_node(dw_hdmi_rockchip_dt_ids, pdev->dev.of_node);
	plat_data = devm_kmemdup(&pdev->dev, match->data,
					     sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	hdmi->dev = &pdev->dev;
	hdmi->chip_data = plat_data->phy_data;
	plat_data->phy_data = hdmi;
	encoder = &hdmi->encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	ret = rockchip_hdmi_parse_dt(hdmi);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to parse OF data\n");
		return ret;
	}

	ret = clk_prepare_enable(hdmi->vpll_clk);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Failed to enable HDMI vpll: %d\n",
			      ret);
		return ret;
	}

	hdmi->phy = devm_phy_optional_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(hdmi->dev, "failed to get phy\n");
		return ret;
	}

	ret = inno_dw_hdmi_init(hdmi);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &dw_hdmi_rockchip_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &dw_hdmi_rockchip_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_bind(pdev, encoder, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		drm_encoder_cleanup(encoder);
		clk_disable_unprepare(hdmi->vpll_clk);
	}

	return ret;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_unbind(hdmi->hdmi);
	clk_disable_unprepare(hdmi->vpll_clk);
}

static const struct component_ops dw_hdmi_rockchip_ops = {
	.bind	= dw_hdmi_rockchip_bind,
	.unbind	= dw_hdmi_rockchip_unbind,
};

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return component_add(&pdev->dev, &dw_hdmi_rockchip_ops);
}

static int dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_rockchip_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused dw_hdmi_rockchip_suspend(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_suspend(hdmi->hdmi);
	pm_runtime_put_sync(dev);

	return 0;
}

static int __maybe_unused dw_hdmi_rockchip_resume(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	dw_hdmi_resume(hdmi->hdmi);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_rockchip_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_hdmi_rockchip_suspend,
				dw_hdmi_rockchip_resume)
};

struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.driver = {
		.name = "dwhdmi-rockchip",
		.pm = &dw_hdmi_rockchip_pm,
		.of_match_table = dw_hdmi_rockchip_dt_ids,
	},
};
