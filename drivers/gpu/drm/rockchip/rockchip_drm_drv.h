/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * based on exynos_drm_drv.h
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ROCKCHIP_DRM_DRV_H
#define _ROCKCHIP_DRM_DRV_H

#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>

#include <linux/module.h>
#include <linux/component.h>

#define ROCKCHIP_MAX_FB_BUFFER	3
#define ROCKCHIP_MAX_CONNECTOR	2
#define ROCKCHIP_MAX_CRTC	2

struct drm_device;
struct drm_connector;
struct iommu_domain;

/*
 * Rockchip drm private crtc funcs.
 * @loader_protect: protect loader logo crtc's power
 * @enable_vblank: enable crtc vblank irq.
 * @disable_vblank: disable crtc vblank irq.
 * @bandwidth: report present crtc bandwidth consume.
 */
struct rockchip_crtc_funcs {
	int (*loader_protect)(struct drm_crtc *crtc, bool on);
	int (*enable_vblank)(struct drm_crtc *crtc);
	void (*disable_vblank)(struct drm_crtc *crtc);
	size_t (*bandwidth)(struct drm_crtc *crtc,
			    struct drm_crtc_state *crtc_state);
	void (*cancel_pending_vblank)(struct drm_crtc *crtc, struct drm_file *file_priv);
	int (*debugfs_init)(struct drm_minor *minor, struct drm_crtc *crtc);
	int (*debugfs_dump)(struct drm_crtc *crtc, struct seq_file *s);
	void (*regs_dump)(struct drm_crtc *crtc, struct seq_file *s);
	enum drm_mode_status (*mode_valid)(struct drm_crtc *crtc,
					   const struct drm_display_mode *mode,
					   int output_type);
};

struct rockchip_atomic_commit {
	struct drm_atomic_state *state;
	struct drm_device *dev;
	size_t bandwidth;
};

struct rockchip_crtc_state {
	struct drm_crtc_state base;
	struct drm_property_blob *cabc_lut;
	struct drm_tv_connector_state *tv_state;
	int left_margin;
	int right_margin;
	int top_margin;
	int bottom_margin;
	int dsp_layer_sel;
	int output_type;
	int output_mode;
	int output_flags;
	int bus_format;
	int yuv_overlay;
	int post_r2y_en;
	int post_y2r_en;
	int post_csc_mode;
	int color_space;
	int eotf;
};

#define to_rockchip_crtc_state(s) \
		container_of(s, struct rockchip_crtc_state, base)

/*
 * Rockchip drm private structure.
 *
 * @crtc: array of enabled CRTCs, used to map from "pipe" to drm_crtc.
 * @num_pipe: number of pipes for this device.
 * @mm_lock: protect drm_mm on multi-threads.
 */
struct rockchip_drm_private {
	struct drm_fb_helper fbdev_helper;
	struct drm_gem_object *fbdev_bo;
	const struct rockchip_crtc_funcs *crtc_funcs[ROCKCHIP_MAX_CRTC];
	struct drm_atomic_state *state;
	struct iommu_domain *domain;
	struct mutex mm_lock;
	struct drm_mm mm;
	struct list_head psr_list;
	spinlock_t psr_list_lock;

	struct rockchip_atomic_commit *commit;
	/* protect async commit */
	struct mutex commit_lock;
	struct work_struct commit_work;
	struct gen_pool *secure_buffer_pool;
	struct devfreq *devfreq;
};

int rockchip_register_crtc_funcs(struct drm_crtc *crtc,
				 const struct rockchip_crtc_funcs *crtc_funcs);
void rockchip_unregister_crtc_funcs(struct drm_crtc *crtc);
int rockchip_drm_dma_attach_device(struct drm_device *drm_dev,
				   struct device *dev);
void rockchip_drm_dma_detach_device(struct drm_device *drm_dev,
				    struct device *dev);
int rockchip_drm_wait_vact_end(struct drm_crtc *crtc, unsigned int mstimeout);

extern struct platform_driver cdn_dp_driver;
extern struct platform_driver dw_hdmi_rockchip_pltfm_driver;
extern struct platform_driver dw_mipi_dsi_driver;
extern struct platform_driver inno_hdmi_driver;
extern struct platform_driver rockchip_dp_driver;
extern struct platform_driver rockchip_lvds_driver;
extern struct platform_driver vop_platform_driver;
#endif /* _ROCKCHIP_DRM_DRV_H_ */
