
/*
 ******************************************************************************
 *
 * sunxi_csi.h
 *
 * Hawkview ISP - sunxi_csi.h module
 *
 * Copyright (c) 2015 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Version		  Author         Date		    Description
 *
 *   3.0		  Yang Feng   	2015/02/27	ISP Tuning Tools Support
 *
 ******************************************************************************
 */

#ifndef _SUNXI_CSI_H_
#define _SUNXI_CSI_H_

#include "../platform/platform_cfg.h"
#include "parser_reg.h"

#define VIDIOC_SUNXI_CSI_SET_CORE_CLK 			1

#define CSI_CORE_CLK_RATE (300*1000*1000)

enum csi_pad {
	CSI_PAD_SINK,
	CSI_PAD_SOURCE,
	CSI_PAD_NUM,
};

enum {
	CSI_CORE_CLK = 0,
	CSI_MASTER_CLK,
	CSI_MISC_CLK,
	CSI_CORE_CLK_SRC,
	CSI_MASTER_CLK_24M_SRC,
	CSI_MASTER_CLK_PLL_SRC,
	CSI_CLK_NUM,
};

#define NOCLK 			0xff

struct csi_format {
	unsigned int wd_align;
	enum v4l2_mbus_pixelcode code;
	enum input_seq seq;
	enum prs_input_fmt infmt;
	unsigned int data_width;
};

struct csi_dev {
	int use_cnt;
	struct v4l2_subdev subdev;
	struct media_pad csi_pads[CSI_PAD_NUM];
	struct platform_device *pdev;
	unsigned int id;
	spinlock_t slock;
	struct mutex subdev_lock;
	int irq;
	wait_queue_head_t wait;
	void __iomem *base;
	struct bus_info bus_info;
	struct frame_info frame_info;
	struct frame_arrange arrange;
	unsigned int cur_ch;
	unsigned int capture_mode;
	struct list_head csi_list;
	struct pinctrl *pctrl;
	struct clk *clock[CSI_CLK_NUM];
	struct v4l2_mbus_framefmt mf;
	struct prs_output_size out_size;
	struct csi_format *csi_fmt;
	struct prs_ncsi_if_cfg ncsi_if;
};

void sunxi_csi_dump_regs(struct v4l2_subdev *sd);
struct v4l2_subdev *sunxi_csi_get_subdev(int id);
int sunxi_csi_platform_register(void);
void sunxi_csi_platform_unregister(void);

#endif /*_SUNXI_CSI_H_*/
