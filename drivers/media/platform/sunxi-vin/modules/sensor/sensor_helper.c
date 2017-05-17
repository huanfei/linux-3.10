
/*
 * sensor_helper.c: helper function for sensors.
 *
 * Copyright (C) 2016       Wei Zhao <zhaowei@allwinnertech.com>
 *
 */

#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/v4l2-subdev.h>
#include <media/v4l2-dev.h>

#include "camera.h"
#include "sensor_helper.h"

struct sensor_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct sensor_info, sd);
}
EXPORT_SYMBOL_GPL(to_state);

void sensor_cfg_req(struct v4l2_subdev *sd, struct sensor_config *cfg)
{
	struct sensor_info *info = to_state(sd);
	if (info == NULL) {
		pr_err("%s is not initialized.n", sd->name);
		return;
	}
	if (info->current_wins == NULL) {
		pr_err("%s format is not initialized.n", sd->name);
		return;
	}

	cfg->width = info->current_wins->width;
	cfg->height = info->current_wins->height;
	cfg->hoffset = info->current_wins->hoffset;
	cfg->voffset = info->current_wins->voffset;
	cfg->hts = info->current_wins->hts;
	cfg->vts = info->current_wins->vts;
	cfg->pclk = info->current_wins->pclk;
	cfg->bin_factor = info->current_wins->bin_factor;
	cfg->intg_min = info->current_wins->intg_min;
	cfg->intg_max = info->current_wins->intg_max;
	cfg->gain_min = info->current_wins->gain_min;
	cfg->gain_max = info->current_wins->gain_max;
}
EXPORT_SYMBOL_GPL(sensor_cfg_req);

int sensor_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct sensor_info *info = to_state(sd);
	if (code->index >= info->fmt_num)
		return -EINVAL;
	code->code = info->fmt_pt[code->index].mbus_code;
	return 0;
}
EXPORT_SYMBOL_GPL(sensor_enum_mbus_code);

int sensor_enum_frame_size(struct v4l2_subdev *sd,
			struct v4l2_subdev_fh *fh,
			struct v4l2_subdev_frame_size_enum *fse)
{
	struct sensor_info *info = to_state(sd);
	if (fse->index >= info->win_size_num)
		return -EINVAL;
	fse->min_width = info->win_pt[fse->index].width;
	fse->max_width = fse->min_width;
	fse->max_height = info->win_pt[fse->index].height;
	fse->min_height = fse->max_height;
	return 0;
}
EXPORT_SYMBOL_GPL(sensor_enum_frame_size);

static struct sensor_format_struct *sensor_find_mbus_code(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *fmt)
{
	struct sensor_info *info = to_state(sd);
	int i;
	for (i = 0; i < info->fmt_num; ++i) {
		if (info->fmt_pt[i].mbus_code == fmt->code)
			break;
	}
	if (i >= info->fmt_num)
		return info->fmt_pt;
	return &info->fmt_pt[i];
}

static struct sensor_win_size *sensor_find_frame_size(struct v4l2_subdev *sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct sensor_info *info = to_state(sd);
	struct sensor_win_size *ws = info->win_pt;
	struct sensor_win_size *best_ws = NULL;
	int best_dist = INT_MAX;
	int i;
	for (i = 0; i < info->win_size_num; ++i) {
		int dist = abs(ws->width - fmt->width) +
		    abs(ws->height - fmt->height);
		if (dist < best_dist) {
			best_dist = dist;
			best_ws = ws;
		}
		++ws;
	}
	return best_ws;
}

static void sensor_fill_mbus_fmt(struct v4l2_subdev *sd,
				struct v4l2_mbus_framefmt *mf,
				const struct sensor_win_size *ws, u32 code)
{
	struct sensor_info *info = to_state(sd);
	mf->width = ws->width;
	mf->height = ws->height;
	mf->code = code;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field = info->sensor_field;
}

static void sensor_try_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt,
				struct sensor_win_size **ws,
				struct sensor_format_struct **sf)
{
	struct sensor_info *info = to_state(sd);
	u32 code = V4L2_MBUS_FMT_YUYV8_2X8;
	if (fmt->pad == SENSOR_PAD_SOURCE) {
		if (info->is_stream) {
			code = info->fmt->mbus_code;
			*ws = info->current_wins;
			*sf = info->fmt;
		} else {
			*ws = sensor_find_frame_size(sd, &fmt->format);
			*sf = sensor_find_mbus_code(sd, &fmt->format);
			code = (*sf)->mbus_code;
		}
	}
	sensor_fill_mbus_fmt(sd, &fmt->format, *ws, code);
}

int sensor_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct sensor_info *info = to_state(sd);
	const struct sensor_win_size *ws;
	u32 code;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(fh, fmt->pad);
		return 0;
	}
	mutex_lock(&info->lock);
	switch (fmt->pad) {
	case SENSOR_PAD_SOURCE:
		code = info->fmt->mbus_code;
		ws = info->current_wins;
		break;
	default:
		mutex_unlock(&info->lock);
		return -EINVAL;
	}
	sensor_fill_mbus_fmt(sd, &fmt->format, ws, code);
	mutex_unlock(&info->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(sensor_get_fmt);

int sensor_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_fh *fh,
			  struct v4l2_subdev_format *fmt)
{
	struct sensor_info *info = to_state(sd);
	struct sensor_win_size *ws = NULL;
	struct sensor_format_struct *sf = NULL;
	struct v4l2_mbus_framefmt *mf;
	int ret = 0;
	mutex_lock(&info->lock);
	pr_info("%s %s %d*%d 0x%x 0x%x\n", sd->name, __func__,
		fmt->format.width, fmt->format.height,
		fmt->format.code, fmt->format.field);
	sensor_try_format(sd, fh, fmt, &ws, &sf);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (NULL == fh) {
			pr_err("%s fh is NULL!\n", sd->name);
			mutex_unlock(&info->lock);
			return -EINVAL;
		}
		mf = v4l2_subdev_get_try_format(fh, fmt->pad);
		*mf = fmt->format;
	} else {
		switch (fmt->pad) {
		case SENSOR_PAD_SOURCE:
			info->current_wins = ws;
			info->fmt = sf;
			break;
		default:
			ret = -EBUSY;
		}
	}
	mutex_unlock(&info->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(sensor_set_fmt);

