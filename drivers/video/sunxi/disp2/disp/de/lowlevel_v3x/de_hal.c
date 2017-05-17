/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */


#include "de_hal.h"

static unsigned int g_device_fps[DE_NUM] = { 60 };
static bool g_de_blank[DE_NUM] = { false };
static unsigned int g_de_freq;
static struct disp_enhance_chn_info ehs_info;

int de_update_device_fps(unsigned int sel, u32 fps)
{
	g_device_fps[sel] = fps;

	return 0;
}

/**
 * Update clk rate of de, unit hz.
 */
int de_update_clk_rate(u32 rate)
{
	g_de_freq = rate / 1000000;

	return 0;
}

/**
 * Get clk rate of de, unit hz.
 */
int de_get_clk_rate(void)
{
	return g_de_freq * 1000000;
}

static int de_set_coarse(unsigned int sel, unsigned char chno, unsigned int fmt,
			 unsigned int lcd_fps, unsigned int lcd_height,
			 unsigned int de_freq_mhz, unsigned int ovl_w,
			 unsigned int ovl_h, unsigned int vsu_outw,
			 unsigned int vsu_outh, unsigned int *midyw,
			 unsigned int *midyh, struct scaler_para *fix_ypara,
			 struct scaler_para *fix_cpara)
{
	int coarse_status;
	unsigned int midcw, midch;

	coarse_status = de_rtmx_set_coarse_fac(sel, chno, fmt, lcd_fps,
					       lcd_height, de_freq_mhz,
					       ovl_w, ovl_h, vsu_outw, vsu_outh,
					       midyw, midyh, &midcw, &midch);
	de_vsu_recalc_scale_para(coarse_status, vsu_outw, vsu_outh,
				 *midyw, *midyh, midcw, midch,
				 fix_ypara, fix_cpara);

	return 0;
}

static int de_calc_overlay_scaler_para(unsigned int screen_id,
		unsigned char chn, unsigned char layno,
		unsigned char *fmt, struct disp_layer_config_data *data,
		unsigned char (*premul)[LAYER_MAX_NUM_PER_CHN],
		unsigned char *premode,
		struct de_rect(*crop)[LAYER_MAX_NUM_PER_CHN],
		struct de_rect(*layer)[LAYER_MAX_NUM_PER_CHN],
		struct de_rect *bld_rect, unsigned int *ovlw,
		unsigned int *ovlh, unsigned char *pen,
		struct scaler_para *ovl_para,
		struct scaler_para *ovl_cpara)
{
	bool scaler_en;
	unsigned char i, j, k, lay_en[CHN_NUM][LAYER_MAX_NUM_PER_CHN];
	unsigned int midyw, midyh;
	unsigned int lcd_fps = g_device_fps[screen_id];
	unsigned int lcd_width = 1280, lcd_height = 720;
	unsigned int de_freq_mhz = g_de_freq;
	struct de_rect64 crop64[CHN_NUM][LAYER_MAX_NUM_PER_CHN];
	struct de_rect frame[CHN_NUM][LAYER_MAX_NUM_PER_CHN];
	static struct scaler_para para[CHN_NUM][LAYER_MAX_NUM_PER_CHN];
	static struct scaler_para cpara[VI_CHN_NUM][LAYER_MAX_NUM_PER_CHN];
	unsigned int vi_chn = de_feat_get_num_vi_chns(screen_id);
	unsigned int scaler_num = de_feat_is_support_scale(screen_id);

	de_rtmx_get_display_size(screen_id, &lcd_width, &lcd_height);
	/* init para */
	for (j = 0; j < vi_chn; j++)
		memset((void *)cpara[j], 0, layno * sizeof(struct scaler_para));
	for (j = 0; j < chn; j++)
		memset((void *)para[j], 0, layno * sizeof(struct scaler_para));

	/* get the original crop frame data */
	for (j = 0, k = 0; j < chn; j++) {
		for (i = 0; i < layno;) {
			memcpy(&crop64[j][i], &data[k].config.info.fb.crop,
			       sizeof(struct disp_rect64));
			memcpy(&frame[j][i], &data[k].config.info.screen_win,
			       sizeof(struct disp_rect));
			lay_en[j][i] = data[k].config.enable;
			premul[j][i] = data[k].config.info.fb.pre_multiply;

			/* 3d mode */
			if (data[k].config.info.fb.flags) {
				memcpy(&crop64[j][i + 1],
				       &data[k].config.info.fb.crop,
				       sizeof(struct disp_rect64));
				de_rtmx_get_3d_in_single_size(
				    (enum de_3d_in_mode)
				    data[k].config.info.fb.flags,
				    &crop64[j][i]);
				if (data[k].config.info.b_trd_out) {
					de_rtmx_get_3d_in_single_size(
					    (enum de_3d_in_mode)
					    data[k].config.info.fb.flags,
					    &crop64[j][i + 1]);
					de_rtmx_get_3d_out(frame[j][i],
					    lcd_width, lcd_height,
					    (enum de_3d_out_mode)
					    data[k].config.info.out_trd_mode,
					    &frame[j][i + 1]);
					lay_en[j][i + 1] =
					    data[k].config.enable;
				} else {
					lay_en[j][i + 1] = 0;
				}
				premul[j][i + 1] =
				    data[k].config.info.fb.pre_multiply;
				k += 2;
				i += 2;
			} else {
				i++;
				k++;
			}
		}
	}

	for (j = 0; j < vi_chn; j++) {
		for (i = 0; i < layno; i++) {
			if (lay_en[j][i])
				de_vsu_calc_scaler_para(fmt[j], crop64[j][i],
				    frame[j][i], &crop[j][i],
				    &para[j][i], &cpara[j][i]);
		}
	}

	for (j = vi_chn; j < chn; j++) {
		for (i = 0; i < layno; i++) {
			if (lay_en[j][i])
				de_gsu_calc_scaler_para(crop64[j][i],
				    frame[j][i], &crop[j][i], &para[j][i]);
		}
	}

	/* calculate the layer coordinate,
	 * overlay size & blending input coordinate
	 */
	for (j = 0; j < chn; j++) {
		int gsu_sel = (j < vi_chn) ? 0 : 1;

		pen[j] =
		    de_rtmx_calc_chnrect(lay_en[j], layno, frame[j], crop[j],
					 gsu_sel, para[j], layer[j],
					 &bld_rect[j], &ovlw[j], &ovlh[j]);
		premode[j] = de_rtmx_get_premul_ctl(layno, premul[j]);
		__inf("ovl_rect[%d]=<%d,%d>\n", j, ovlw[j], ovlh[j]);
		__inf("bld_rect[%d]=<%d,%d,%d,%d>\n", j,
		      bld_rect[j].x, bld_rect[j].y,
		      bld_rect[j].w, bld_rect[j].h);
	}

	/* get video overlay parameter for scaler */
	for (j = 0; j < vi_chn; j++) {
		scaler_en = 0x1;
		if ((fmt[j] == 0) && (ovlw[j] == bld_rect[j].w)
		    && (ovlh[j] == bld_rect[j].h)) {
			scaler_en = 0x0;
		}
		if (scaler_en)
			de_vsu_sel_ovl_scaler_para(lay_en[j], para[j], cpara[j],
			    &ovl_para[j], &ovl_cpara[j]);

		/*
		 * recalculate overlay size, blending coordinate,
		 * blending size, layer coordinate
		 */
		de_recalc_ovl_bld_for_scale(scaler_en, lay_en[j], layno,
					    &ovl_para[j], layer[j],
					    &bld_rect[j], &ovlw[j], &ovlh[j], 0,
					    lcd_width, lcd_height);

		de_set_coarse(screen_id, j, fmt[j], lcd_fps, lcd_height,
			      de_freq_mhz, ovlw[j], ovlh[j],
			      bld_rect[j].w, bld_rect[j].h,
			      &midyw, &midyh, &ovl_para[j], &ovl_cpara[j]);
		de_vsu_set_para(screen_id, j, scaler_en, fmt[j], midyw, midyh,
				bld_rect[j].w, bld_rect[j].h,
				&ovl_para[j], &ovl_cpara[j]);
	}

	/* get ui overlay parameter for scaler */
	for (j = vi_chn; j < scaler_num; j++) {
		scaler_en = 0x1;
		if ((ovlw[j] == bld_rect[j].w) && (ovlh[j] == bld_rect[j].h))
			scaler_en = 0x0;
		if (scaler_en)
			de_gsu_sel_ovl_scaler_para(lay_en[j], para[j],
						   &ovl_para[j]);

		/* recalculate overlay size, blending coordinate,
		 * blending size, layer coordinate
		 */
		de_recalc_ovl_bld_for_scale(scaler_en, lay_en[j], layno,
					    &ovl_para[j], layer[j],
					    &bld_rect[j], &ovlw[j], &ovlh[j], 1,
					    lcd_width, lcd_height);

		de_gsu_set_para(screen_id, j, scaler_en, ovlw[j], ovlh[j],
				bld_rect[j].w, bld_rect[j].h, &ovl_para[j]);
	}

	return 0;
}

int de_get_layer_config(struct disp_layer_config_data *data,
			unsigned char chn, unsigned char layno,
			struct de_rect(*crop)[LAYER_MAX_NUM_PER_CHN],
			struct de_rect(*layer)[LAYER_MAX_NUM_PER_CHN],
			unsigned char (*premul)[LAYER_MAX_NUM_PER_CHN],
			struct __lay_para_t *cfg)
{
	unsigned char i, j, k;

	for (j = 0, k = 0; j < chn; j++) {
		for (i = 0; i < layno;) {
			cfg[k].en = data[k].config.enable;
			cfg[k].alpha_mode = data[k].config.info.alpha_mode;
			cfg[k].alpha = data[k].config.info.alpha_value;
			cfg[k].fcolor_en = data[k].config.info.mode;
			cfg[k].fmt = data[k].config.info.fb.format;
			cfg[k].premul_ctl = premul[j][i];

			cfg[k].pitch[0] = data[k].config.info.fb.size[0].width;
			cfg[k].pitch[1] = data[k].config.info.fb.size[1].width;
			cfg[k].pitch[2] = data[k].config.info.fb.size[2].width;
			cfg[k].layer = layer[j][i];
			cfg[k].laddr_t[0] =
			    (data[k].config.info.fb.addr[0] & 0xFFFFFFFF);
			cfg[k].laddr_t[1] =
			    (data[k].config.info.fb.addr[1] & 0xFFFFFFFF);
			cfg[k].laddr_t[2] =
			    (data[k].config.info.fb.addr[2] & 0xFFFFFFFF);

			cfg[k].top_bot_en = 0x0;
			cfg[k].laddr_b[0] = 0x0;
			cfg[k].laddr_b[1] = 0x0;
			cfg[k].laddr_b[2] = 0x0;

			/* 3d mode */
			if (data[k].config.info.fb.flags) {
				if (data[k].config.info.b_trd_out)
					cfg[k + 1].en = data[k].config.enable;
				else
					cfg[k + 1].en = 0;

				cfg[k + 1].alpha_mode =
				    data[k].config.info.alpha_mode;
				cfg[k + 1].alpha =
				    data[k].config.info.alpha_value;
				cfg[k + 1].fcolor_en = data[k].config.info.mode;
				cfg[k + 1].fmt = data[k].config.info.fb.format;
				cfg[k + 1].premul_ctl = premul[j][i];

				cfg[k + 1].layer = layer[j][i + 1];
				de_rtmx_get_3d_in(data[k].config.info.fb.format,
					crop[j][i + 1],
					(struct de_fb *) data[k].config.info.
					fb.size, data[k].config.info.fb.align,
					(enum de_3d_in_mode) data[k].
					config.info.fb.flags, cfg[k].laddr_t,
					data[k].config.info.fb.trd_right_addr,
					cfg[k].pitch, cfg[k + 1].pitch,
					cfg[k + 1].laddr_t);

				cfg[k + 1].top_bot_en = cfg[k].top_bot_en;
				cfg[k + 1].laddr_b[0] = cfg[k].laddr_b[0];
				cfg[k + 1].laddr_b[1] = cfg[k].laddr_b[1];
				cfg[k + 1].laddr_b[2] = cfg[k].laddr_b[2];
				data[k + 1].flag = data[k].flag;
				k += 2;
				i += 2;
			} else {
				i++;
				k++;
			}
		}
	}
	return 0;
}

int de_al_lyr_apply(unsigned int screen_id, struct disp_layer_config_data *data,
		    unsigned int layer_num,
		    struct disp_csc_config *mgr_csc_cfg)
{
	unsigned char i, j, k, chn, vi_chn, layno;
	unsigned char haddr[LAYER_MAX_NUM_PER_CHN][3];
	unsigned char premul[CHN_NUM][LAYER_MAX_NUM_PER_CHN], format[CHN_NUM],
	    premode[CHN_NUM], zoder[CHN_NUM] = { 0, 1, 2 }, pen[CHN_NUM];
	unsigned int ovlw[CHN_NUM], ovlh[CHN_NUM];
	static struct __lay_para_t cfg[CHN_NUM * LAYER_MAX_NUM_PER_CHN];
	struct de_rect layer[CHN_NUM][LAYER_MAX_NUM_PER_CHN], bld_rect[CHN_NUM];
	struct de_rect crop[CHN_NUM][LAYER_MAX_NUM_PER_CHN];
	static struct scaler_para ovl_para[CHN_NUM], ovl_cpara[VI_CHN_NUM];
	bool chn_used[CHN_NUM] = { false }, chn_zorder_cfg[CHN_NUM] = { false },
	    chn_dirty[CHN_NUM] = { false };
	bool chn_is_yuv[CHN_NUM] = { false };
	enum disp_color_space cs[CHN_NUM] = {0};
	unsigned char layer_zorder[CHN_NUM] = { 0 }, chn_index;
	unsigned char pipe_used[CHN_NUM] = { 0 };
	unsigned int pipe_sel[CHN_NUM] = { 0 };
	struct de_rect pipe_rect[CHN_NUM] = { {0} };
	struct disp_rect dispsize[CHN_NUM] = { {0} };
	struct disp_layer_config_data *data1;

	data1 = data;

	chn = de_feat_get_num_chns(screen_id);
	vi_chn = de_feat_get_num_vi_chns(screen_id);
	layno = LAYER_MAX_NUM_PER_CHN;

	memset(&ehs_info, 0, sizeof(struct disp_enhance_chn_info));
	/* parse zorder of channel */
	data1 = data;
	for (i = 0; i < layer_num; i++, data1++) {
		if (!data1->config.enable)
			continue;

		chn_used[data1->config.channel] = true;
		if (data1->config.info.fb.format >=
		    DISP_FORMAT_YUV444_I_AYUV) {
			chn_is_yuv[data1->config.channel] = true;
			cs[data1->config.channel] =
			    data1->config.info.fb.color_space;
		}
		if (data1->flag)
			chn_dirty[data1->config.channel] = true;

		layer_zorder[data1->config.channel] = data1->config.info.zorder;

		/* update color space */
		if ((cs[data1->config.channel] == DISP_UNDEF) ||
		    (cs[data1->config.channel] == DISP_RESERVED)) {

			cs[data1->config.channel] = DISP_BT709;
			if ((data1->config.info.fb.size[0].width <= 736) &&
			    (data1->config.info.fb.size[0].height <= 576))
				cs[data1->config.channel] = DISP_BT601;
		} else if ((cs[data1->config.channel] == DISP_UNDEF_F) ||
		    (cs[data1->config.channel] == DISP_RESERVED_F)) {

			cs[data1->config.channel] = DISP_BT709_F;
			if ((data1->config.info.fb.size[0].width <= 736) &&
			    (data1->config.info.fb.size[0].height <= 576))
				cs[data1->config.channel] = DISP_BT601_F;
		}

		/* rgb format will be always in full range */
		if (data1->config.info.fb.format < DISP_FORMAT_YUV444_I_AYUV)
			cs[data1->config.channel] = DISP_GBR_F;

		if (data1->config.channel < vi_chn) {
			struct disp_enhance_layer_info *ehs_layer_info =
				&ehs_info.layer_info[data1->config.layer_id];

			ehs_layer_info->fb_size.width =
			    data1->config.info.fb.size[0].width;
			ehs_layer_info->fb_size.height =
			    data1->config.info.fb.size[0].height;
			ehs_layer_info->fb_crop.x =
			    data1->config.info.fb.crop.x >> 32;
			ehs_layer_info->fb_crop.y =
			    data1->config.info.fb.crop.y >> 32;
			ehs_layer_info->en = 1;
			ehs_layer_info->format = data1->config.info.fb.format;
		}
	}

	data1 = data;
	for (i = 0; i < layer_num; i++) {
		if (data1->config.enable && chn_dirty[data1->config.channel])
			data1->flag = LAYER_ALL_DIRTY;
		data1++;
	}

	chn_index = 0;
	for (i = 0; i < chn; i++) {
		u32 min_zorder = 255, min_zorder_chn = 0;
		bool find = false;

		for (j = 0; j < chn; j++) {
			if ((true == chn_used[j]) && (true != chn_zorder_cfg[j]
			    && (min_zorder > layer_zorder[j]))) {
				min_zorder = layer_zorder[j];
				min_zorder_chn = j;
				find = true;
			}
		}
		if (find) {
			chn_zorder_cfg[min_zorder_chn] = true;
			zoder[min_zorder_chn] = chn_index++;
		}
	}

	/* parse zorder of pipe */
	for (i = 0; i < chn; i++) {
		if (chn_used[i]) {
			u32 pipe_index = zoder[i];

			pipe_used[pipe_index] = (g_de_blank[screen_id]) ?
			    false : true;
			pipe_sel[pipe_index] = i;
		}
	}
	for (i = 0; i < chn; i++)
		__inf("ch%d z %d %s\n", i, zoder[i],
		      chn_used[i] ? "en" : "dis");
	for (i = 0; i < chn; i++)
		__inf("pipe%d z %d %s\n", i, pipe_sel[i],
		      pipe_used[i] ? "en" : "dis");

	/* init para */
	for (j = 0; j < chn; j++)
		memset((void *)crop[j], 0x0, layno * sizeof(struct de_rect));

	/* check the video format for fill color,
	 * because of the hardware limit
	 */
	for (j = 0, k = 0; j < vi_chn; j++) {
		format[j] = 0;
		for (i = 0; i < layno; i++) {
			if (data[k].config.info.fb.format >=
				    DISP_FORMAT_YUV422_I_YVYU)
				format[j] = data[k].config.info.fb.format;
			k++;
		}
		__inf("format[%d]=%d\n", j, format[j]);
	}

	de_calc_overlay_scaler_para(screen_id, chn, layno, format, data, premul,
				    premode, crop, layer, bld_rect, ovlw, ovlh,
				    pen, ovl_para, ovl_cpara);

	for (j = 0; j < chn; j++) {
		if (chn_used[j]) {
			struct disp_csc_config csc_cfg;

			csc_cfg.in_fmt = (chn_is_yuv[j]) ? DE_YUV : DE_RGB;
			/*
			 * TBD : 1.DISP_UNDEF/DISP_UNDEF_F/DISP_RESERVED/
			 * DISP_RESERVED_F process
			 * 2. confirm output 3 para
			 */
			csc_cfg.in_mode = cs[j] & 0xF;
			csc_cfg.in_color_range = ((cs[j] & 0xF00) == 0x100) ?
			    DISP_COLOR_RANGE_16_235 : DISP_COLOR_RANGE_0_255;
			csc_cfg.out_fmt = mgr_csc_cfg->out_fmt;
			csc_cfg.out_mode = mgr_csc_cfg->out_mode & 0xF;
			csc_cfg.out_color_range = mgr_csc_cfg->out_color_range;
			de_ccsc_apply(screen_id, j, &csc_cfg);
		}
	}

	/* init cfg from layer config */
	de_get_layer_config(data, chn, layno, crop, layer, premul, cfg);

	for (j = 0, k = 0; j < chn; j++) {
		for (i = 0; i < layno; i++) {
			if (LAYER_SIZE_DIRTY & data[k + i].flag) {
				de_rtmx_set_overlay_size(screen_id, j, ovlw[j],
							 ovlh[j]);
				break;
			}
		}

		for (i = 0; i < layno; i++) {
			if (LAYER_ATTR_DIRTY & data[k].flag) {
				de_rtmx_set_lay_cfg(screen_id, j, i,
						    &cfg[k]);
				de_rtmx_set_lay_laddr(screen_id, j, i,
						      cfg[k].fmt,
						      crop[j][i],
						      cfg[k].pitch,
						      data[k].config.info.fb.
						      align,
			(enum de_3d_in_mode)data[k].config.info.fb.flags,
						      cfg[k].laddr_t,
						      haddr[i]);
			}
			if (LAYER_VI_FC_DIRTY & data[k].flag) {
				de_rtmx_set_lay_fcolor(screen_id, j, i,
						       data[k].config.info.mode,
						       format[j],
						       data[k].config.info.color
						       );
			}
			if (LAYER_HADDR_DIRTY & data[k].flag) {
				cfg[k].haddr_t[0] =
				    ((data[k].config.info.fb.
				      addr[0] >> 32) & 0xFF) + haddr[i][0];
				cfg[k].haddr_t[1] =
				    ((data[k].config.info.fb.
				      addr[1] >> 32) & 0xFF) + haddr[i][1];
				cfg[k].haddr_t[2] =
				    ((data[k].config.info.fb.
				      addr[2] >> 32) & 0xFF) + haddr[i][2];

				cfg[k].haddr_b[0] = 0x0;
				cfg[k].haddr_b[1] = 0x0;
				cfg[k].haddr_b[2] = 0x0;
				de_rtmx_set_lay_haddr(screen_id, j, i,
						      cfg[k].top_bot_en,
						      cfg[k].haddr_t,
						      cfg[k].haddr_b);
			}
			k++;
		}
	}

	/* parse pipe rect */
	for (i = 0; i < chn; i++) {
		if (pipe_used[i]) {
			u32 chn_index = pipe_sel[i];

			memcpy(&pipe_rect[i], &bld_rect[chn_index],
			       sizeof(struct disp_rect));
			dispsize[i].width = bld_rect[i].w;
			dispsize[i].height = bld_rect[i].h;
		}
	}
	/* need route information to calculate pipe enable and input size */
	de_rtmx_set_pf_en(screen_id, pipe_used);
	for (i = 0; i < chn; i++) {
		__inf("sel=%d, pipe_rect[%d]=<%d,%d,%d,%d>\n", screen_id, i,
		      pipe_rect[i].x, pipe_rect[i].y,
		      pipe_rect[i].w, pipe_rect[i].h);
		de_rtmx_set_pipe_cfg(screen_id, i, 0xff000000, pipe_rect[i]);
		de_rtmx_set_route(screen_id, i, pipe_sel[i]);
		de_rtmx_set_premul(screen_id, i, premode[i]);
	}

	for (i = 0; i < chn - 1; i++)
		de_rtmx_set_blend_mode(screen_id, i, DE_BLD_SRCOVER);
	/* de_rtmx_set_colorkey(screen_id,); */

	ehs_info.ovl_size.width = ovlw[0];
	ehs_info.ovl_size.height = ovlh[0];
	ehs_info.bld_size.width = bld_rect[0].w;
	ehs_info.bld_size.height = bld_rect[0].h;
	/* set enhance size */
	de_enhance_layer_apply(screen_id, &ehs_info);

	return 0;
}

int de_al_mgr_apply(unsigned int screen_id, struct disp_manager_data *data)
{
	unsigned char tmp;
	int color = (data->config.back_color.alpha << 24) |
	    (data->config.back_color.red << 16) |
	    (data->config.back_color.green << 8) |
	    (data->config.back_color.blue << 0);

	g_de_blank[screen_id] = data->config.blank;

	if (data->flag & MANAGER_BACK_COLOR_DIRTY)
		de_rtmx_set_background_color(screen_id, color);
	if (data->flag & MANAGER_SIZE_DIRTY) {
		de_rtmx_set_blend_size(screen_id, data->config.size.width,
				       data->config.size.height);
		de_rtmx_set_display_size(screen_id, data->config.size.width,
					 data->config.size.height);
	}
	if (data->flag & MANAGER_ENABLE_DIRTY) {
		de_rtmx_set_enable(screen_id, data->config.enable);
#if !defined(HAVE_DEVICE_COMMON_MODULE)
		de_rtmx_mux(screen_id, data->config.hwdev_index);
#endif
		de_rtmx_set_outitl(screen_id, data->config.interlace);
	}

	if (data->flag & MANAGER_COLOR_SPACE_DIRTY) {
		/* set yuv or rgb blending space */
		tmp = (data->config.cs == DISP_CSC_TYPE_RGB) ? 0 : 1;
		de_rtmx_set_bld_color_space(screen_id, tmp);
	}

	return 0;
}

int de_al_mgr_sync(unsigned int screen_id)
{
	/* double register switch */
	return de_rtmx_set_dbuff_rdy(screen_id);
}

int de_al_mgr_update_regs(unsigned int screen_id)
{
	int ret = 0;

	de_rtmx_update_regs(screen_id);
	de_vsu_update_regs(screen_id);
	de_gsu_update_regs(screen_id);
	de_ccsc_update_regs(screen_id);

	return ret;
}

/* query irq, if irq coming, return 1, and clear irq flga */
int de_al_query_irq(unsigned int screen_id)
{
	return de_rtmx_query_irq(screen_id);
}

int de_al_enable_irq(unsigned int screen_id, unsigned en)
{
	return de_rtmx_enable_irq(screen_id, en);
}

int de_al_init(disp_bsp_init_para *para)
{
	int i;
	int num_screens = de_feat_get_num_screens();

	for (i = 0; i < num_screens; i++) {
		de_rtmx_init(i, para->reg_base[DISP_MOD_DE]);
		de_vsu_init(i, para->reg_base[DISP_MOD_DE]);
		de_gsu_init(i, para->reg_base[DISP_MOD_DE]);
	}

	return 0;
}
