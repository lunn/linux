// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Coda multi-standard codec IP - JPEG support functions
 *
 * Copyright (C) 2014 Philipp Zabel, Pengutronix
 */

#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/swab.h>
#include <linux/videodev2.h>

#include <media/v4l2-common.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "coda.h"
#include "trace.h"

#define SOI_MARKER	0xffd8
#define APP9_MARKER	0xffe9
#define DRI_MARKER	0xffdd
#define DQT_MARKER	0xffdb
#define DHT_MARKER	0xffc4
#define SOF_MARKER	0xffc0
#define SOS_MARKER	0xffda
#define EOI_MARKER	0xffd9

enum {
	CODA9_JPEG_FORMAT_420,
	CODA9_JPEG_FORMAT_422,
	CODA9_JPEG_FORMAT_224,
	CODA9_JPEG_FORMAT_444,
	CODA9_JPEG_FORMAT_400,
};

struct coda_huff_tab {
	u8	dc_bits[2][16];
	u8	dc_values[2][12 + 4]; /* padded to 32-bit */
	u8	ac_bits[2][16];
	u8	ac_values[2][162 + 2]; /* padded to 32-bit */

	/* DC Luma, DC Chroma, AC Luma, AC Chroma */
	s16	min[4 * 16];
	s16	max[4 * 16];
	s8	ptr[4 * 16];
};

/*
 * Typical Huffman tables for 8-bit precision luminance and
 * chrominance from JPEG ITU-T.81 (ISO/IEC 10918-1) Annex K.3
 */

static const unsigned char luma_dc_bits[16] = {
	0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char luma_dc_value[12] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b,
};

static const unsigned char chroma_dc_bits[16] = {
	0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const unsigned char chroma_dc_value[12] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b,
};

static const unsigned char luma_ac_bits[16] = {
	0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
	0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d,
};

static const unsigned char luma_ac_value[162 + 2] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa, /* padded to 32-bit */
};

static const unsigned char chroma_ac_bits[16] = {
	0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
	0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
};

static const unsigned char chroma_ac_value[162 + 2] = {
	0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
	0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
	0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
	0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
	0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
	0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
	0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
	0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
	0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
	0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
	0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
	0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
	0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
	0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa, /* padded to 32-bit */
};

/*
 * Quantization tables for luminance and chrominance components in
 * zig-zag scan order from the Freescale i.MX VPU libraries
 */

static unsigned char luma_q[64] = {
	0x06, 0x04, 0x04, 0x04, 0x05, 0x04, 0x06, 0x05,
	0x05, 0x06, 0x09, 0x06, 0x05, 0x06, 0x09, 0x0b,
	0x08, 0x06, 0x06, 0x08, 0x0b, 0x0c, 0x0a, 0x0a,
	0x0b, 0x0a, 0x0a, 0x0c, 0x10, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x10, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
};

static unsigned char chroma_q[64] = {
	0x07, 0x07, 0x07, 0x0d, 0x0c, 0x0d, 0x18, 0x10,
	0x10, 0x18, 0x14, 0x0e, 0x0e, 0x0e, 0x14, 0x14,
	0x0e, 0x0e, 0x0e, 0x0e, 0x14, 0x11, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x11, 0x11, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x11, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
	0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
};

static const unsigned char width_align[] = {
	[CODA9_JPEG_FORMAT_420] = 16,
	[CODA9_JPEG_FORMAT_422] = 16,
	[CODA9_JPEG_FORMAT_224] = 8,
	[CODA9_JPEG_FORMAT_444] = 8,
	[CODA9_JPEG_FORMAT_400] = 8,
};

static const unsigned char height_align[] = {
	[CODA9_JPEG_FORMAT_420] = 16,
	[CODA9_JPEG_FORMAT_422] = 8,
	[CODA9_JPEG_FORMAT_224] = 16,
	[CODA9_JPEG_FORMAT_444] = 8,
	[CODA9_JPEG_FORMAT_400] = 8,
};

static int coda9_jpeg_chroma_format(u32 pixfmt)
{
	switch (pixfmt) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
		return CODA9_JPEG_FORMAT_420;
	case V4L2_PIX_FMT_YUV422P:
		return CODA9_JPEG_FORMAT_422;
	case V4L2_PIX_FMT_YUV444:
		return CODA9_JPEG_FORMAT_444;
	case V4L2_PIX_FMT_GREY:
		return CODA9_JPEG_FORMAT_400;
	}
	return -EINVAL;
}

struct coda_memcpy_desc {
	int offset;
	const void *src;
	size_t len;
};

static void coda_memcpy_parabuf(void *parabuf,
				const struct coda_memcpy_desc *desc)
{
	u32 *dst = parabuf + desc->offset;
	const u32 *src = desc->src;
	int len = desc->len / 4;
	int i;

	for (i = 0; i < len; i += 2) {
		dst[i + 1] = swab32(src[i]);
		dst[i] = swab32(src[i + 1]);
	}
}

int coda_jpeg_write_tables(struct coda_ctx *ctx)
{
	int i;
	static const struct coda_memcpy_desc huff[8] = {
		{ 0,   luma_dc_bits,    sizeof(luma_dc_bits)    },
		{ 16,  luma_dc_value,   sizeof(luma_dc_value)   },
		{ 32,  luma_ac_bits,    sizeof(luma_ac_bits)    },
		{ 48,  luma_ac_value,   sizeof(luma_ac_value)   },
		{ 216, chroma_dc_bits,  sizeof(chroma_dc_bits)  },
		{ 232, chroma_dc_value, sizeof(chroma_dc_value) },
		{ 248, chroma_ac_bits,  sizeof(chroma_ac_bits)  },
		{ 264, chroma_ac_value, sizeof(chroma_ac_value) },
	};
	struct coda_memcpy_desc qmat[3] = {
		{ 512, ctx->params.jpeg_qmat_tab[0], 64 },
		{ 576, ctx->params.jpeg_qmat_tab[1], 64 },
		{ 640, ctx->params.jpeg_qmat_tab[1], 64 },
	};

	/* Write huffman tables to parameter memory */
	for (i = 0; i < ARRAY_SIZE(huff); i++)
		coda_memcpy_parabuf(ctx->parabuf.vaddr, huff + i);

	/* Write Q-matrix to parameter memory */
	for (i = 0; i < ARRAY_SIZE(qmat); i++)
		coda_memcpy_parabuf(ctx->parabuf.vaddr, qmat + i);

	return 0;
}

bool coda_jpeg_check_buffer(struct coda_ctx *ctx, struct vb2_buffer *vb)
{
	void *vaddr = vb2_plane_vaddr(vb, 0);
	u16 soi, eoi;
	int len, i;

	soi = be16_to_cpup((__be16 *)vaddr);
	if (soi != SOI_MARKER)
		return false;

	len = vb2_get_plane_payload(vb, 0);
	vaddr += len - 2;
	for (i = 0; i < 32; i++) {
		eoi = be16_to_cpup((__be16 *)(vaddr - i));
		if (eoi == EOI_MARKER) {
			if (i > 0)
				vb2_set_plane_payload(vb, 0, len - i);
			return true;
		}
	}

	return false;
}

static int coda9_jpeg_parse_dri_header(struct coda_ctx *ctx, u8 *buf, u8 *end)
{
	int len;

	len = be16_to_cpup((__be16 *)buf);
	if (buf + len >= end)
		return -EINVAL;
	buf += 2;
	if (len < 4)
		return -EINVAL;

	ctx->params.jpeg_restart_interval = be16_to_cpup((__be16 *)buf);

	return len;
}

static int coda9_jpeg_parse_dqt_header(struct coda_ctx *ctx, u8 *buf, u8 *end)
{
	struct coda_dev *dev = ctx->dev;
	int index;
	int len;

	len = be16_to_cpup((__be16 *)buf);
	if (buf + len >= end)
		return -EINVAL;
	buf += 2;
	if (len < 3 + 64)
		return -EINVAL;

	index = *buf++;
	if (index > 2) {
		v4l2_err(&dev->v4l2_dev, "quantization table index > 2: %d\n",
			 index);
		return -EINVAL;
	}

	if (!ctx->params.jpeg_qmat_tab[index])
		ctx->params.jpeg_qmat_tab[index] = kmalloc(64, GFP_KERNEL);
	memcpy(ctx->params.jpeg_qmat_tab[index], buf, 64);

	return len;
}

static int coda9_jpeg_parse_dht_header(struct coda_ctx *ctx, u8 *buf, u8 *end)
{
	struct coda_dev *dev = ctx->dev;
	struct coda_huff_tab *huff_tab;
	u8 *huff_bits, *huff_values;
	int num_values;
	int index;
	int len;

	len = be16_to_cpup((__be16 *)buf);
	if (buf + len >= end)
		return -EINVAL;
	buf += 2;
	if (len < 3 + 16 + 12)
		return -EINVAL;

	index = *buf++; /* Luma/Chroma DC/AC */
	if ((index & 0x10) && len < 3 + 16 + 162)
		return -EINVAL;

	huff_tab = ctx->params.jpeg_huff_tab;
	if (!huff_tab) {
		huff_tab = kzalloc(sizeof(struct coda_huff_tab), GFP_KERNEL);
		if (!huff_tab)
			return -ENOMEM;
		ctx->params.jpeg_huff_tab = huff_tab;
	}

	switch (index) {
	case 0x00:
		huff_bits = huff_tab->dc_bits[0];
		huff_values = huff_tab->dc_values[0];
		num_values = 12;
		break;
	case 0x10:
		huff_bits = huff_tab->ac_bits[0];
		huff_values = huff_tab->ac_values[0];
		num_values = 162;
		break;
	case 0x01:
		huff_bits = huff_tab->dc_bits[1];
		huff_values = huff_tab->dc_values[1];
		num_values = 12;
		break;
	case 0x11:
		huff_bits = huff_tab->ac_bits[1];
		huff_values = huff_tab->ac_values[1];
		num_values = 162;
		break;
	default:
		v4l2_err(&dev->v4l2_dev, "invalid huffman table: 0x%x\n",
			 index);
		return -EINVAL;
	}

	memcpy(huff_bits, buf, 16);
	buf += 16;
	memcpy(huff_values, buf, num_values);
	buf += num_values;

	return len;
}

static int coda9_jpeg_parse_sof_header(struct coda_ctx *ctx, u8 *buf, u8 *end)
{
	struct coda_dev *dev = ctx->dev;
	struct coda_q_data *q_data_src;
	int sample_precision;
	int width, height;
	int comp_num;
	int len;
	int i;

	len = be16_to_cpup((__be16 *)buf);
	if (buf + len >= end)
		return -EINVAL;
	buf += 2;
	if (len < 8 + 3)
		return -EINVAL;

	sample_precision = *buf++;
	if (sample_precision != 8) {
		v4l2_err(&dev->v4l2_dev, "invalid sample precision: %d\n",
			 sample_precision);
		return -EINVAL;
	}

	height = be16_to_cpup((__be16 *)buf);
	buf += 2;
	width = be16_to_cpup((__be16 *)buf);
	buf += 2;

	if (height > ctx->codec->max_h || width > ctx->codec->max_w) {
		v4l2_err(&dev->v4l2_dev, "invalid dimensions: %dx%d\n",
			 width, height);
		return -EINVAL;
	}

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (height != q_data_src->height || width != q_data_src->width) {
		v4l2_err(&dev->v4l2_dev,
			 "dimensions don't match format: %dx%d\n",
			 width, height);
		return -EINVAL;
	}

	comp_num = *buf++;
	if (comp_num != 1 && comp_num != 3) {
		v4l2_err(&dev->v4l2_dev, "invalid number of components: %d\n",
			 comp_num);
		return -EINVAL;
	}

	if (len < 8 + 3 * comp_num)
		return -EINVAL;

	for (i = 0; i < comp_num; i++) {
		u8 subsampling;

		ctx->params.jpeg_comp_id[i] = *buf++;

		subsampling = *buf++;
		ctx->params.jpeg_chroma_subsampling[i] = subsampling;

		if (subsampling != 0x22 && subsampling != 0x21 &&
		    subsampling != 0x12 && subsampling != 0x11) {
			v4l2_err(&dev->v4l2_dev,
				 "invalid subsampling: 0x%02x\n", subsampling);
			return -EINVAL;
		}

		ctx->params.jpeg_qmat_index[i] = *buf++;
	}

	return len;
}

static int coda9_jpeg_parse_sos_header(struct coda_ctx *ctx, u8 *buf, u8 *end)
{
	struct coda_dev *dev = ctx->dev;
	int comp_num;
	int len;
	int i, j;
	u8 tmp, ss, se, ah_al;

	len = be16_to_cpup((__be16 *)buf);
	if (buf + len >= end)
		return -EINVAL;
	buf += 2;

	if (len < 6 + 2)
		return -EINVAL;

	comp_num = *buf++;
	if (comp_num != 1 && comp_num != 3) {
		v4l2_err(&dev->v4l2_dev, "invalid number of components: %d\n",
			 comp_num);
		return -EINVAL;
	}

	if (len < 6 + 2 * comp_num)
		return -EINVAL;

	for (i = 0; i < comp_num; i++) {
		unsigned int comp_id = *buf++;

		for (j = 0; j < comp_num; j++) {
			if (comp_id == ctx->params.jpeg_comp_id[j])
				break;
		}
		if (j >= comp_num) {
			v4l2_err(&dev->v4l2_dev, "unknown component id: %d\n",
				 comp_id);
			return -EINVAL;
		}

		tmp = *buf++;

		ctx->params.jpeg_huff_dc_index[j] = (tmp >> 4) & 0xf;
		ctx->params.jpeg_huff_ac_index[j] = tmp & 0xf;
	}

	ss = *buf++;
	se = *buf++;
	ah_al = *buf++;

	if ((ss != 0) || (se != 0x3f) || (ah_al != 0)) {
		v4l2_err(&dev->v4l2_dev, "invalid JPEG profile\n");
		return -EINVAL;
	}

	return len;
}

static int coda9_jpeg_gen_dec_huff_tab(struct coda_ctx *ctx, int tab_num);

int coda_jpeg_decode_header(struct coda_ctx *ctx, struct vb2_buffer *vb)
{
	u8 *buf = vb2_plane_vaddr(vb, 0);
	size_t len = vb2_get_plane_payload(vb, 0);
	u8 *end = buf + len;
	u8 *start = buf;
	int marker;
	int ret;
	int i;

	marker = be16_to_cpup((__be16 *)buf);
	if (marker != SOI_MARKER)
		return -EINVAL;

	while (++buf < end) {
		if (buf[-1] != 0xff || buf[0] == 0xff || buf[0] == 0x00)
			continue;

		marker = be16_to_cpup((__be16 *)(buf - 1));
		buf++;

		switch (marker) {
		case SOI_MARKER:
			/* SOI marker is empty, just skip it */
			ret = 0;
			break;
		case DRI_MARKER:
			ret = coda9_jpeg_parse_dri_header(ctx, buf, end);
			break;
		case DQT_MARKER:
			ret = coda9_jpeg_parse_dqt_header(ctx, buf, end);
			break;
		case DHT_MARKER:
			ret = coda9_jpeg_parse_dht_header(ctx, buf, end);
			break;
		case SOF_MARKER:
			ret = coda9_jpeg_parse_sof_header(ctx, buf, end);
			break;
		case SOS_MARKER:
			ret = coda9_jpeg_parse_sos_header(ctx, buf, end);
			if (ret < 0)
				return ret;
			/* Entropy coded segment begins after SOF header */
			ctx->jpeg_ecs_offset = buf + ret - start;
			/* skip to end */
			buf = end;
			break;
		case EOI_MARKER:
			/* this should never be reached */
			v4l2_warn(&ctx->dev->v4l2_dev,
				  "reached EOI marker at %ld\n", (long)(buf - start));
			return -EINVAL;
		case APP9_MARKER:
			/* possibly used as sequence counter, ignore */
		default:
			ret = 0;
		}

		if (ret < 0)
			return ret;
		buf += ret;
	}

	/* Generate Huffman table information */
	for (i = 0; i < 4; i++)
		coda9_jpeg_gen_dec_huff_tab(ctx, i);

	if (ctx->params.jpeg_chroma_subsampling[0] == 0x21 &&
	    ctx->params.jpeg_chroma_subsampling[1] == 0x11 &&
	    ctx->params.jpeg_chroma_subsampling[2] == 0x11) {
		ctx->params.jpeg_format = 1;
	}

	return 0;
}

static inline void coda9_jpeg_write_huff_values(struct coda_dev *dev, u8 *bits,
						s8 *values, int num_values)
{
	int huff_length, i;

	for (huff_length = 0, i = 0; i < 16; i++)
		huff_length += bits[i];
	for (i = huff_length; i < num_values; i++)
		values[i] = -1;
	for (i = 0; i < num_values; i++)
		coda_write(dev, (s32)values[i], CODA9_REG_JPEG_HUFF_DATA);
}

static int coda9_jpeg_dec_huff_setup(struct coda_ctx *ctx)
{
	struct coda_huff_tab *huff_tab = ctx->params.jpeg_huff_tab;
	struct coda_dev *dev = ctx->dev;
	s16 *huff_min = huff_tab->min;
	s16 *huff_max = huff_tab->max;
	s8 *huff_ptr = huff_tab->ptr;
	int i;

	/* MIN Tables */
	coda_write(dev, 0x003, CODA9_REG_JPEG_HUFF_CTRL);
	coda_write(dev, 0x000, CODA9_REG_JPEG_HUFF_ADDR);
	for (i = 0; i < 4 * 16; i++)
		coda_write(dev, (s32)huff_min[i], CODA9_REG_JPEG_HUFF_DATA);

	/* MAX Tables */
	coda_write(dev, 0x403, CODA9_REG_JPEG_HUFF_CTRL);
	coda_write(dev, 0x440, CODA9_REG_JPEG_HUFF_ADDR);
	for (i = 0; i < 4 * 16; i++)
		coda_write(dev, (s32)huff_max[i], CODA9_REG_JPEG_HUFF_DATA);

	/* PTR Tables */
	coda_write(dev, 0x803, CODA9_REG_JPEG_HUFF_CTRL);
	coda_write(dev, 0x880, CODA9_REG_JPEG_HUFF_ADDR);
	for (i = 0; i < 4 * 16; i++)
		coda_write(dev, (s32)huff_ptr[i], CODA9_REG_JPEG_HUFF_DATA);

	/* VAL Tables: DC Luma, DC Chroma, AC Luma, AC Chroma */
	coda_write(dev, 0xc03, CODA9_REG_JPEG_HUFF_CTRL);
	coda9_jpeg_write_huff_values(dev, huff_tab->dc_bits[0],
					  huff_tab->dc_values[0], 12);
	coda9_jpeg_write_huff_values(dev, huff_tab->dc_bits[1],
					  huff_tab->dc_values[1], 12);
	coda9_jpeg_write_huff_values(dev, huff_tab->ac_bits[0],
					  huff_tab->ac_values[0], 162);
	coda9_jpeg_write_huff_values(dev, huff_tab->ac_bits[1],
					  huff_tab->ac_values[1], 162);
	coda_write(dev, 0x000, CODA9_REG_JPEG_HUFF_CTRL);
	return 0;
}

static inline void coda9_jpeg_write_qmat_tab(struct coda_dev *dev,
					     u8 *qmat, int index)
{
	int i;

	coda_write(dev, index | 0x3, CODA9_REG_JPEG_QMAT_CTRL);
	for (i = 0; i < 64; i++)
		coda_write(dev, qmat[i], CODA9_REG_JPEG_QMAT_DATA);
	coda_write(dev, 0, CODA9_REG_JPEG_QMAT_CTRL);
}

static void coda9_jpeg_qmat_setup(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	int *qmat_index = ctx->params.jpeg_qmat_index;
	u8 **qmat_tab = ctx->params.jpeg_qmat_tab;

	coda9_jpeg_write_qmat_tab(dev, qmat_tab[qmat_index[0]], 0x00);
	coda9_jpeg_write_qmat_tab(dev, qmat_tab[qmat_index[1]], 0x40);
	coda9_jpeg_write_qmat_tab(dev, qmat_tab[qmat_index[2]], 0x80);
}

static void coda9_jpeg_dec_bbc_gbu_setup(struct coda_ctx *ctx,
					 struct vb2_buffer *buf, u32 ecs_offset)
{
	struct coda_dev *dev = ctx->dev;
	int page_ptr, word_ptr, bit_ptr;
	u32 bbc_base_addr, end_addr;
	int bbc_cur_pos;
	int ret, val;

	bbc_base_addr = vb2_dma_contig_plane_dma_addr(buf, 0);
	end_addr = bbc_base_addr + vb2_get_plane_payload(buf, 0);

	page_ptr = ecs_offset / 256;
	word_ptr = (ecs_offset % 256) / 4;
	if (page_ptr & 1)
		word_ptr += 64;
	bit_ptr = (ecs_offset % 4) * 8;
	if (word_ptr & 1)
		bit_ptr += 32;
	word_ptr &= ~0x1;

	coda_write(dev, end_addr, CODA9_REG_JPEG_BBC_WR_PTR);
	coda_write(dev, bbc_base_addr, CODA9_REG_JPEG_BBC_BAS_ADDR);

	/* Leave 3 256-byte page margin to avoid a BBC interrupt */
	coda_write(dev, end_addr + 256 * 3 + 256, CODA9_REG_JPEG_BBC_END_ADDR);
	val = DIV_ROUND_UP(vb2_plane_size(buf, 0), 256) + 3;
	coda_write(dev, BIT(31) | val, CODA9_REG_JPEG_BBC_STRM_CTRL);

	bbc_cur_pos = page_ptr;
	coda_write(dev, bbc_cur_pos, CODA9_REG_JPEG_BBC_CUR_POS);
	coda_write(dev, bbc_base_addr + (bbc_cur_pos << 8),
			CODA9_REG_JPEG_BBC_EXT_ADDR);
	coda_write(dev, (bbc_cur_pos & 1) << 6, CODA9_REG_JPEG_BBC_INT_ADDR);
	coda_write(dev, 64, CODA9_REG_JPEG_BBC_DATA_CNT);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_COMMAND);
	do {
		ret = coda_read(dev, CODA9_REG_JPEG_BBC_BUSY);
	} while (ret == 1);

	bbc_cur_pos++;
	coda_write(dev, bbc_cur_pos, CODA9_REG_JPEG_BBC_CUR_POS);
	coda_write(dev, bbc_base_addr + (bbc_cur_pos << 8),
			CODA9_REG_JPEG_BBC_EXT_ADDR);
	coda_write(dev, (bbc_cur_pos & 1) << 6, CODA9_REG_JPEG_BBC_INT_ADDR);
	coda_write(dev, 64, CODA9_REG_JPEG_BBC_DATA_CNT);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_COMMAND);
	do {
		ret = coda_read(dev, CODA9_REG_JPEG_BBC_BUSY);
	} while (ret == 1);

	bbc_cur_pos++;
	coda_write(dev, bbc_cur_pos, CODA9_REG_JPEG_BBC_CUR_POS);
	coda_write(dev, 1, CODA9_REG_JPEG_BBC_CTRL);

	coda_write(dev, 0, CODA9_REG_JPEG_GBU_TT_CNT);
	coda_write(dev, word_ptr, CODA9_REG_JPEG_GBU_WD_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_BBSR);
	coda_write(dev, 127, CODA9_REG_JPEG_GBU_BBER);
	if (page_ptr & 1) {
		coda_write(dev, 0, CODA9_REG_JPEG_GBU_BBIR);
		coda_write(dev, 0, CODA9_REG_JPEG_GBU_BBHR);
	} else {
		coda_write(dev, 64, CODA9_REG_JPEG_GBU_BBIR);
		coda_write(dev, 64, CODA9_REG_JPEG_GBU_BBHR);
	}
	coda_write(dev, 4, CODA9_REG_JPEG_GBU_CTRL);
	coda_write(dev, bit_ptr, CODA9_REG_JPEG_GBU_FF_RPTR);
	coda_write(dev, 3, CODA9_REG_JPEG_GBU_CTRL);
}

static const int bus_req_num[] = {
	[CODA9_JPEG_FORMAT_420] = 2,
	[CODA9_JPEG_FORMAT_422] = 3,
	[CODA9_JPEG_FORMAT_224] = 3,
	[CODA9_JPEG_FORMAT_444] = 4,
	[CODA9_JPEG_FORMAT_400] = 4,
};

#define MCU_INFO(mcu_block_num, comp_num, comp0_info, comp1_info, comp2_info) \
	(((mcu_block_num) << CODA9_JPEG_MCU_BLOCK_NUM_OFFSET) | \
	 ((comp_num) << CODA9_JPEG_COMP_NUM_OFFSET) | \
	 ((comp0_info) << CODA9_JPEG_COMP0_INFO_OFFSET) | \
	 ((comp1_info) << CODA9_JPEG_COMP1_INFO_OFFSET) | \
	 ((comp2_info) << CODA9_JPEG_COMP2_INFO_OFFSET))

static const u32 mcu_info[] = {
	[CODA9_JPEG_FORMAT_420] = MCU_INFO(6, 3, 10, 5, 5),
	[CODA9_JPEG_FORMAT_422] = MCU_INFO(4, 3, 9, 5, 5),
	[CODA9_JPEG_FORMAT_224] = MCU_INFO(4, 3, 6, 5, 5),
	[CODA9_JPEG_FORMAT_444] = MCU_INFO(3, 3, 5, 5, 5),
	[CODA9_JPEG_FORMAT_400] = MCU_INFO(1, 1, 5, 0, 0),
};

static int coda9_jpeg_gen_enc_huff_tab(struct coda_ctx *ctx, int tab_num,
				       int (*huff_size)[256],
				       int (*huff_code)[256])
{
	int p, i, l, lastp, si, maxsymbol;
	const u8 *bitleng, *huffval;
	int *ehufco, *ehufsi;
	int *huffsize;
	int *huffcode;
	int code;
	int ret;

	const unsigned char *ld_la_cd_ca_bits[4] = {
		luma_dc_bits, luma_ac_bits, chroma_dc_bits, chroma_ac_bits,
	};
	const unsigned char *ld_la_cd_ca_value[4] = {
		luma_dc_value, luma_ac_value, chroma_dc_value, chroma_ac_value,
	};

	huffsize = kzalloc(sizeof(int) * 256, GFP_KERNEL);
	huffcode = kzalloc(sizeof(int) * 256, GFP_KERNEL);

	bitleng = ld_la_cd_ca_bits[tab_num];
	huffval = ld_la_cd_ca_value[tab_num];
	ehufco  = (int *)huff_code;
	ehufsi  = (int *)huff_size;

	maxsymbol = tab_num & 1 ? 256 : 16;

	/* Figure C.1: make table of Huffman code length for each symbol */
	p = 0;
	for (l = 1; l <= 16; l++) {
		i = bitleng[l-1];
		if (i < 0 || p + i > maxsymbol) {
			ret = -EINVAL;
			goto out;
		}
		while (i--)
			huffsize[p++] = l;
	}
	lastp = p;

	/*
	 * Figure C.2: generate the codes themselves
	 * We also validate that the counts represent a legal Huffman code
	 * tree.
	 */
	code = 0;
	si = huffsize[0];
	p = 0;
	while (p < lastp) {
		while (huffsize[p] == si) {
			huffcode[p++] = code;
			code++;
		}
		if (code >= (1 << si)) {
			ret = -EINVAL;
			goto out;
		}
		code <<= 1;
		si++;
	}

	/* Figure C.3: generate encoding tables */
	/* These are code and size indexed by symbol value */
	memset(ehufsi, 0, sizeof(int) * 256);
	memset(ehufco, 0, sizeof(int) * 256);

	for (p = 0; p < lastp; p++) {
		i = huffval[p];
		if (i < 0 || i >= maxsymbol || ehufsi[i]) {
			ret = -EINVAL;
			goto out;
		}
		ehufco[i] = huffcode[p];
		ehufsi[i] = huffsize[p];
	}

	ret = 0;
out:
	kfree(huffsize);
	kfree(huffcode);
	return ret;
}

#define DC_TABLE_INDEX0		    0
#define AC_TABLE_INDEX0		    1
#define DC_TABLE_INDEX1		    2
#define AC_TABLE_INDEX1		    3

static u8 *coda9_jpeg_get_huff_bits(struct coda_ctx *ctx, int tab_num)
{
	struct coda_huff_tab *huff_tab = ctx->params.jpeg_huff_tab;

	if (!huff_tab)
		return NULL;

	switch (tab_num) {
	case DC_TABLE_INDEX0: return huff_tab->dc_bits[0];
	case AC_TABLE_INDEX0: return huff_tab->ac_bits[0];
	case DC_TABLE_INDEX1: return huff_tab->dc_bits[1];
	case AC_TABLE_INDEX1: return huff_tab->ac_bits[1];
	}

	return NULL;
}

static int coda9_jpeg_gen_dec_huff_tab(struct coda_ctx *ctx, int tab_num)
{
	int ptr_cnt = 0, huff_code = 0, zero_flag = 0, data_flag = 0;
	u8 *huff_bits;
	s16 *huff_max;
	s16 *huff_min;
	s8 *huff_ptr;
	int ofs;
	int i;

	huff_bits = coda9_jpeg_get_huff_bits(ctx, tab_num);
	if (!huff_bits)
		return -EINVAL;

	/* DC/AC Luma, DC/AC Chroma -> DC Luma/Chroma, AC Luma/Chroma */
	ofs = ((tab_num & 1) << 1) | ((tab_num >> 1) & 1);
	ofs *= 16;

	huff_ptr = ctx->params.jpeg_huff_tab->ptr + ofs;
	huff_max = ctx->params.jpeg_huff_tab->max + ofs;
	huff_min = ctx->params.jpeg_huff_tab->min + ofs;

	for (i = 0; i < 16; i++) {
		if (huff_bits[i]) {
			huff_ptr[i] = ptr_cnt;
			ptr_cnt += huff_bits[i];
			huff_min[i] = huff_code;
			huff_max[i] = huff_code + (huff_bits[i] - 1);
			data_flag = 1;
			zero_flag = 0;
		} else {
			huff_ptr[i] = -1;
			huff_min[i] = -1;
			huff_max[i] = -1;
			zero_flag = 1;
		}

		if (data_flag == 1) {
			if (zero_flag == 1)
				huff_code <<= 1;
			else
				huff_code = (huff_max[i] + 1) << 1;
		}
	}

	return 0;
}

static int coda9_jpeg_load_huff_tab(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	int (*huff_size)[256];
	int (*huff_code)[256];
	int i, j, t;
	int huffData;
	int ret;

	huff_size = kzalloc(sizeof(int) * 4 * 256, GFP_KERNEL);
	huff_code = kzalloc(sizeof(int) * 4 * 256, GFP_KERNEL);

	/* Generate all four (luma/chroma DC/AC) code/size lookup tables */
	for (i = 0; i < 4; i++) {
		ret = coda9_jpeg_gen_enc_huff_tab(ctx, i, &huff_size[i],
						  &huff_code[i]);
		if (ret)
			goto out;
	}

	coda_write(dev, 0x3, CODA9_REG_JPEG_HUFF_CTRL);

	for (j = 0; j < 4; j++) {
		t = (j == 0) ? AC_TABLE_INDEX0 : (j == 1) ?
		     AC_TABLE_INDEX1 : (j == 2) ? DC_TABLE_INDEX0
						: DC_TABLE_INDEX1;
		for (i = 0; i < 256; i++) {
			/* DC tables only have 16 entries */
			if ((t == DC_TABLE_INDEX0 || t == DC_TABLE_INDEX1) &&
			    (i > 15))
				break;

			if ((huff_size[t][i] == 0) &&
			    (huff_code[t][i] == 0)) {
				huffData = 0;
			} else {
				huffData = (huff_size[t][i] - 1);
				huffData = (huffData << 16) | (huff_code[t][i]);
			}
			coda_write(dev, huffData, CODA9_REG_JPEG_HUFF_DATA);
		}
	}
	coda_write(dev, 0x0, CODA9_REG_JPEG_HUFF_CTRL);

	ret = 0;
out:
	kfree(huff_size);
	kfree(huff_code);
	return ret;
}

static inline void coda9_jpeg_write_qmat_quotients(struct coda_dev *dev,
						   u8 *qmat, int index)
{
	int i;

	coda_write(dev, index | 0x3, CODA9_REG_JPEG_QMAT_CTRL);
	for (i = 0; i < 64; i++)
		coda_write(dev, 0x80000 / qmat[i], CODA9_REG_JPEG_QMAT_DATA);
	coda_write(dev, index, CODA9_REG_JPEG_QMAT_CTRL);
}

static void coda_scale_quant_table(u8 *q_tab, int scale);

static int coda9_jpeg_load_qmat_tab(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	u8 *luma_tab;
	u8 *chroma_tab;

	luma_tab = ctx->params.jpeg_qmat_tab[0];
	if (!luma_tab)
		luma_tab = luma_q;

	chroma_tab = ctx->params.jpeg_qmat_tab[1];
	if (!chroma_tab)
		chroma_tab = chroma_q;

	coda9_jpeg_write_qmat_quotients(dev, luma_tab, 0x00);
	coda9_jpeg_write_qmat_quotients(dev, chroma_tab, 0x40);
	coda9_jpeg_write_qmat_quotients(dev, chroma_tab, 0x80);

	return 0;
}

#define PUT_BYTE(byte) \
	do { if (tot++ > len) return 0; \
	     *buf++ = (u8)(byte); } while (0)
#define PUT_WORD(word) \
	do { PUT_BYTE((word) >> 8); \
	     PUT_BYTE((word) & 0xff); } while (0)

static int coda9_jpeg_encode_header(struct coda_ctx *ctx, int len, u8 *buf)
{
	struct coda_q_data *q_data_src;
	int chroma_format, comp_num;
	int i, tot, pad;

	tot = 0;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	chroma_format = coda9_jpeg_chroma_format(q_data_src->fourcc);
	if (chroma_format < 0)
		return 0;

	/* Start Of Image */
	PUT_WORD(SOI_MARKER);

	/* Define Restart Interval */
	if (ctx->params.jpeg_restart_interval) {
		PUT_WORD(DRI_MARKER);
		PUT_WORD(4);
		PUT_WORD(ctx->params.jpeg_restart_interval);
	}

	/* Define Quantization Tables */
	PUT_WORD(DQT_MARKER);
	PUT_WORD(3 + 64);
	PUT_BYTE(0x00); /* Luma */
	for (i = 0; i < 64; i++)
		PUT_BYTE(ctx->params.jpeg_qmat_tab[0][i]);
	if (chroma_format != CODA9_JPEG_FORMAT_400) {
		PUT_WORD(DQT_MARKER);
		PUT_WORD(3 + 64);
		PUT_BYTE(0x01); /* Chroma */
		for (i = 0; i < 64; i++)
			PUT_BYTE(ctx->params.jpeg_qmat_tab[1][i]);
	}

	/* Define Huffman Tables */
	PUT_WORD(DHT_MARKER);
	PUT_WORD(3 + 16 + 12);
	PUT_BYTE(0x00); /* Luma DC */
	for (i = 0; i < 16; i++)
		PUT_BYTE(luma_dc_bits[i]);
	for (i = 0; i < 12; i++)
		PUT_BYTE(luma_dc_value[i]);
	PUT_WORD(DHT_MARKER);
	PUT_WORD(3 + 16 + 162);
	PUT_BYTE(0x10); /* Luma AC */
	for (i = 0; i < 16; i++)
		PUT_BYTE(luma_ac_bits[i]);
	for (i = 0; i < 162; i++)
		PUT_BYTE(luma_ac_value[i]);

	if (chroma_format != CODA9_JPEG_FORMAT_400) {
		PUT_WORD(DHT_MARKER);
		PUT_WORD(3 + 16 + 12);
		PUT_BYTE(0x01); /* Chroma DC */
		for (i = 0; i < 16; i++)
			PUT_BYTE(chroma_dc_bits[i]);
		for (i = 0; i < 12; i++)
			PUT_BYTE(chroma_dc_value[i]);

		PUT_WORD(DHT_MARKER);
		PUT_WORD(3 + 16 + 162);
		PUT_BYTE(0x11); /* Chroma AC */
		for (i = 0; i < 16; i++)
			PUT_BYTE(chroma_ac_bits[i]);
		for (i = 0; i < 162; i++)
			PUT_BYTE(chroma_ac_value[i]);
	}

	/* Start Of Frame */
	PUT_WORD(SOF_MARKER);
	comp_num = (chroma_format == CODA9_JPEG_FORMAT_400) ? 1 : 3;
	PUT_WORD(8 + comp_num * 3);
	PUT_BYTE(0x08);
	PUT_WORD(q_data_src->height);
	PUT_WORD(q_data_src->width);
	PUT_BYTE(comp_num);
	for (i = 0; i < comp_num; i++) {
		static unsigned char subsampling[5][3] = {
			[CODA9_JPEG_FORMAT_420] = { 0x22, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_422] = { 0x21, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_224] = { 0x12, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_444] = { 0x11, 0x11, 0x11 },
			[CODA9_JPEG_FORMAT_400] = { 0x11 },
		};

		PUT_BYTE(i + 1); /* Component identifier, matches SOS */
		PUT_BYTE(subsampling[chroma_format][i]);
		PUT_BYTE((i == 0) ? 0 : 1); /* Chroma table index */
	}

	/* Pad to multiple of 8 bytes */
	pad = tot % 8;
	if (pad) {
		pad = 8 - pad;
		while (pad--)
			PUT_BYTE(0x00);
	}

	return tot;
}

/*
 * Scale quantization table using nonlinear scaling factor
 * u8 qtab[64], scale [50,190]
 */
static void coda_scale_quant_table(u8 *q_tab, int scale)
{
	unsigned int temp;
	int i;

	for (i = 0; i < 64; i++) {
		temp = DIV_ROUND_CLOSEST((unsigned int)q_tab[i] * scale, 100);
		if (temp <= 0)
			temp = 1;
		if (temp > 255)
			temp = 255;
		q_tab[i] = (unsigned char)temp;
	}
}

void coda_set_jpeg_compression_quality(struct coda_ctx *ctx, int quality)
{
	unsigned int scale;

	ctx->params.jpeg_quality = quality;

	/* Clip quality setting to [5,100] interval */
	if (quality > 100)
		quality = 100;
	if (quality < 5)
		quality = 5;

	/*
	 * Non-linear scaling factor:
	 * [5,50] -> [1000..100], [51,100] -> [98..0]
	 */
	if (quality < 50)
		scale = 5000 / quality;
	else
		scale = 200 - 2 * quality;

	if (ctx->params.jpeg_qmat_tab[0]) {
		memcpy(ctx->params.jpeg_qmat_tab[0], luma_q, 64);
		coda_scale_quant_table(ctx->params.jpeg_qmat_tab[0], scale);
	}
	if (ctx->params.jpeg_qmat_tab[1]) {
		memcpy(ctx->params.jpeg_qmat_tab[1], chroma_q, 64);
		coda_scale_quant_table(ctx->params.jpeg_qmat_tab[1], scale);
	}
}

/*
 * Encoder context operations
 */

static int coda9_jpeg_start_encoding(struct coda_ctx *ctx)
{
	if (!ctx->params.jpeg_qmat_tab[0])
		ctx->params.jpeg_qmat_tab[0] = kmalloc(64, GFP_KERNEL);
	if (!ctx->params.jpeg_qmat_tab[1])
		ctx->params.jpeg_qmat_tab[1] = kmalloc(64, GFP_KERNEL);
	coda_set_jpeg_compression_quality(ctx, ctx->params.jpeg_quality);

	return 0;
}

static int coda9_jpeg_prepare_encode(struct coda_ctx *ctx)
{
	struct coda_q_data *q_data_src, *q_data_dst;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct coda_dev *dev = ctx->dev;
	unsigned int start_addr, end_addr;
	int aligned_width, aligned_height;
	int chroma_interleave;
	int chroma_format;
	int header_len;
	int ret;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	src_buf->sequence = ctx->osequence;
	dst_buf->sequence = ctx->osequence;
	ctx->osequence++;

	src_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	src_buf->flags &= ~V4L2_BUF_FLAG_PFRAME;

	coda_set_gdi_regs(ctx);

	start_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	end_addr = start_addr + vb2_plane_size(&dst_buf->vb2_buf, 0);

	chroma_format = coda9_jpeg_chroma_format(q_data_src->fourcc);
	if (chroma_format < 0)
		return chroma_format;

	/* Round image dimensions to multiple of MCU size */
	aligned_width = round_up(q_data_src->width, width_align[chroma_format]);
	aligned_height = round_up(q_data_src->height,
				  height_align[chroma_format]);
	if (aligned_width != q_data_src->bytesperline) {
		v4l2_err(&dev->v4l2_dev, "WRONG STRIDE: %d vs %d\n",
			 aligned_width, q_data_src->bytesperline);
	}

	header_len = coda9_jpeg_encode_header(ctx,
					vb2_plane_size(&dst_buf->vb2_buf, 0),
					vb2_plane_vaddr(&dst_buf->vb2_buf, 0));

	coda_write(dev, start_addr + header_len, CODA9_REG_JPEG_BBC_BAS_ADDR);
	coda_write(dev, end_addr, CODA9_REG_JPEG_BBC_END_ADDR);
	coda_write(dev, start_addr + header_len, CODA9_REG_JPEG_BBC_WR_PTR);
	coda_write(dev, start_addr + header_len, CODA9_REG_JPEG_BBC_RD_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_CUR_POS);
	/* 64 words per 256-byte page */
	coda_write(dev, 64, CODA9_REG_JPEG_BBC_DATA_CNT);
	coda_write(dev, start_addr, CODA9_REG_JPEG_BBC_EXT_ADDR);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_INT_ADDR);

	coda_write(dev, 0, CODA9_REG_JPEG_GBU_BT_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_WD_PTR);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_BBSR);
	coda_write(dev, 0, CODA9_REG_JPEG_BBC_STRM_CTRL);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_CTRL);
	coda_write(dev, 0, CODA9_REG_JPEG_GBU_FF_RPTR);
	coda_write(dev, 127, CODA9_REG_JPEG_GBU_BBER);
	coda_write(dev, 64, CODA9_REG_JPEG_GBU_BBIR);
	coda_write(dev, 64, CODA9_REG_JPEG_GBU_BBHR);

	chroma_interleave = (q_data_src->fourcc == V4L2_PIX_FMT_NV12);
	coda_write(dev, CODA9_JPEG_PIC_CTRL_TC_DIRECTION |
		   CODA9_JPEG_PIC_CTRL_ENCODER_EN, CODA9_REG_JPEG_PIC_CTRL);
	coda_write(dev, 0, CODA9_REG_JPEG_SCL_INFO);
	coda_write(dev, chroma_interleave, CODA9_REG_JPEG_DPB_CONFIG);
	coda_write(dev, ctx->params.jpeg_restart_interval,
			CODA9_REG_JPEG_RST_INTVAL);
	coda_write(dev, 1, CODA9_REG_JPEG_BBC_CTRL);

	coda_write(dev, bus_req_num[chroma_format], CODA9_REG_JPEG_OP_INFO);

	if (ctx->osequence < 2) {
		ret = coda9_jpeg_load_huff_tab(ctx);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "error loading huff tab\n");
			return ret;
		}

		ret = coda9_jpeg_load_qmat_tab(ctx);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev, "error loading qmat tab\n");
			return ret;
		}
	}

	if (ctx->params.rot_mode & CODA_ROT_90) {
		aligned_width = aligned_height;
		aligned_height = q_data_src->bytesperline;
		if (chroma_format == CODA9_JPEG_FORMAT_422)
			chroma_format = CODA9_JPEG_FORMAT_224;
		else if (chroma_format == CODA9_JPEG_FORMAT_224)
			chroma_format = CODA9_JPEG_FORMAT_422;
	}
	/* These need to be multiples of MCU size */
	coda_write(dev, aligned_width << 16 | aligned_height,
		   CODA9_REG_JPEG_PIC_SIZE);
	coda_write(dev, ctx->params.rot_mode ?
		   (CODA_ROT_MIR_ENABLE | ctx->params.rot_mode) : 0,
		   CODA9_REG_JPEG_ROT_INFO);

	coda_write(dev, mcu_info[chroma_format], CODA9_REG_JPEG_MCU_INFO);

	coda_write(dev, 1, CODA9_GDI_CONTROL);
	do {
		ret = coda_read(dev, CODA9_GDI_STATUS);
	} while (!ret);

	coda_write(dev, (chroma_format << 17) | (chroma_interleave << 16) |
		   q_data_src->bytesperline, CODA9_GDI_INFO_CONTROL);
	/* The content of this register seems to be irrelevant: */
	coda_write(dev, aligned_width << 16 | aligned_height,
		   CODA9_GDI_INFO_PIC_SIZE);

	coda_write_base(ctx, q_data_src, src_buf, CODA9_GDI_INFO_BASE_Y);

	coda_write(dev, 0, CODA9_REG_JPEG_DPB_BASE00);
	coda_write(dev, 0, CODA9_GDI_CONTROL);
	coda_write(dev, 1, CODA9_GDI_PIC_INIT_HOST);

	coda_write(dev, 1, CODA9_GDI_WPROT_ERR_CLR);
	coda_write(dev, 0, CODA9_GDI_WPROT_RGN_EN);

	trace_coda_jpeg_run(ctx, src_buf);
	coda_stats_run(dev->jpeg_stats);

	coda_write(dev, 1, CODA9_REG_JPEG_PIC_START);

	return 0;
}

static void coda9_jpeg_finish_encode(struct coda_ctx *ctx)
{
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct coda_dev *dev = ctx->dev;
	u32 wr_ptr, start_ptr;
	u32 err_mb;

	/*
	 * Lock to make sure that an encoder stop command running in parallel
	 * will either already have marked src_buf as last, or it will wake up
	 * the capture queue after the buffers are returned.
	 */
	mutex_lock(&ctx->wakeup_mutex);
	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	trace_coda_jpeg_done(ctx, dst_buf);

	/*
	 * Set plane payload to the number of bytes written out
	 * by the JPEG processing unit
	 */
	start_ptr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	wr_ptr = coda_read(dev, CODA9_REG_JPEG_BBC_WR_PTR);
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, wr_ptr - start_ptr);

	err_mb = coda_read(dev, CODA9_REG_JPEG_PIC_ERRMB);
	if (err_mb)
		coda_dbg(1, ctx, "ERRMB: 0x%x\n", err_mb);

	coda_write(dev, 0, CODA9_REG_JPEG_BBC_FLUSH_CMD);

	dst_buf->flags &= ~(V4L2_BUF_FLAG_PFRAME | V4L2_BUF_FLAG_LAST);
	dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	dst_buf->flags |= src_buf->flags & V4L2_BUF_FLAG_LAST;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, false);

	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
	coda_m2m_buf_done(ctx, dst_buf, err_mb ? VB2_BUF_STATE_ERROR :
						 VB2_BUF_STATE_DONE);
	mutex_unlock(&ctx->wakeup_mutex);

	coda_dbg(1, ctx, "job finished: encoded frame (%u)%s\n",
		 dst_buf->sequence,
		 (dst_buf->flags & V4L2_BUF_FLAG_LAST) ? " (last)" : "");

	/*
	 * Reset JPEG processing unit after each encode run to work
	 * around hangups when switching context between encoder and
	 * decoder.
	 */
	coda_hw_reset(ctx);
}

static void coda9_jpeg_release(struct coda_ctx *ctx)
{
	int i;

	if (ctx->params.jpeg_qmat_tab[0] == luma_q)
		ctx->params.jpeg_qmat_tab[0] = NULL;
	if (ctx->params.jpeg_qmat_tab[1] == chroma_q)
		ctx->params.jpeg_qmat_tab[1] = NULL;
	for (i = 0; i < 3; i++)
		kfree(ctx->params.jpeg_qmat_tab[i]);
	kfree(ctx->params.jpeg_huff_tab);
}

const struct coda_context_ops coda9_jpeg_encode_ops = {
	.queue_init = coda_encoder_queue_init,
	.start_streaming = coda9_jpeg_start_encoding,
	.prepare_run = coda9_jpeg_prepare_encode,
	.finish_run = coda9_jpeg_finish_encode,
	.release = coda9_jpeg_release,
};

/*
 * Decoder context operations
 */

static int coda9_jpeg_start_decoding(struct coda_ctx *ctx)
{
	ctx->params.jpeg_qmat_index[0] = 0;
	ctx->params.jpeg_qmat_index[1] = 1;
	ctx->params.jpeg_qmat_index[2] = 1;
	ctx->params.jpeg_qmat_tab[0] = luma_q;
	ctx->params.jpeg_qmat_tab[1] = chroma_q;
	/* nothing more to do here */

	/* TODO: we could already scan the first header to get the chroma
	 * format.
	 */

	return 0;
}

static int coda9_jpeg_prepare_decode(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	int aligned_width, aligned_height;
	int chroma_format;
	int ret;
	u32 val, dst_fourcc;
	struct coda_q_data *q_data_dst;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	int chroma_interleave;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	dst_fourcc = q_data_dst->fourcc;

	chroma_format = coda9_jpeg_chroma_format(q_data_dst->fourcc);
	if (chroma_format < 0) {
		v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
		return chroma_format;
	}

	/* Round image dimensions to multiple of MCU size */
	aligned_width = round_up(q_data_dst->width, width_align[chroma_format]);
	aligned_height = round_up(q_data_dst->height,
				  height_align[chroma_format]);
	if (aligned_width != q_data_dst->bytesperline) {
		v4l2_err(&dev->v4l2_dev, "stride mismatch: %d != %d\n",
			 aligned_width, q_data_dst->bytesperline);
	}

	coda_set_gdi_regs(ctx);

	ret = coda_jpeg_decode_header(ctx, &src_buf->vb2_buf);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "failed to decode JPEG header: %d\n",
			 ret);

		src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

		v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
		return ret;
	}

	val = ctx->params.jpeg_huff_ac_index[0] << 12 |
	      ctx->params.jpeg_huff_ac_index[1] << 11 |
	      ctx->params.jpeg_huff_ac_index[2] << 10 |
	      ctx->params.jpeg_huff_dc_index[0] << 9 |
	      ctx->params.jpeg_huff_dc_index[1] << 8 |
	      ctx->params.jpeg_huff_dc_index[2] << 7;
	if (ctx->params.jpeg_huff_tab)
		val |= CODA9_JPEG_PIC_CTRL_USER_HUFFMAN_EN;
	coda_write(dev, val, CODA9_REG_JPEG_PIC_CTRL);

	coda_write(dev, aligned_width << 16 | aligned_height,
			CODA9_REG_JPEG_PIC_SIZE);

	chroma_interleave = (dst_fourcc == V4L2_PIX_FMT_NV12);
	coda_write(dev, 0, CODA9_REG_JPEG_ROT_INFO);
	coda_write(dev, bus_req_num[chroma_format], CODA9_REG_JPEG_OP_INFO);
	coda_write(dev, mcu_info[chroma_format], CODA9_REG_JPEG_MCU_INFO);
	coda_write(dev, 0, CODA9_REG_JPEG_SCL_INFO);
	coda_write(dev, chroma_interleave, CODA9_REG_JPEG_DPB_CONFIG);
	coda_write(dev, ctx->params.jpeg_restart_interval,
			CODA9_REG_JPEG_RST_INTVAL);

	if (ctx->params.jpeg_huff_tab) {
		ret = coda9_jpeg_dec_huff_setup(ctx);
		if (ret < 0) {
			v4l2_err(&dev->v4l2_dev,
				 "failed to set up Huffman tables: %d\n", ret);
			v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
			return ret;
		}
	}

	coda9_jpeg_qmat_setup(ctx);

	coda9_jpeg_dec_bbc_gbu_setup(ctx, &src_buf->vb2_buf,
				     ctx->jpeg_ecs_offset);

	coda_write(dev, 0, CODA9_REG_JPEG_RST_INDEX);
	coda_write(dev, 0, CODA9_REG_JPEG_RST_COUNT);

	coda_write(dev, 0, CODA9_REG_JPEG_DPCM_DIFF_Y);
	coda_write(dev, 0, CODA9_REG_JPEG_DPCM_DIFF_CB);
	coda_write(dev, 0, CODA9_REG_JPEG_DPCM_DIFF_CR);

	coda_write(dev, 0, CODA9_REG_JPEG_ROT_INFO);

	coda_write(dev, 1, CODA9_GDI_CONTROL);
	do {
		ret = coda_read(dev, CODA9_GDI_STATUS);
	} while (!ret);

	val = (chroma_format << 17) | (chroma_interleave << 16) |
	      q_data_dst->bytesperline;
	if (ctx->tiled_map_type == GDI_TILED_FRAME_MB_RASTER_MAP)
		val |= 3 << 20;
	coda_write(dev, val, CODA9_GDI_INFO_CONTROL);

	coda_write(dev, aligned_width << 16 | aligned_height,
			CODA9_GDI_INFO_PIC_SIZE);

	coda_write_base(ctx, q_data_dst, dst_buf, CODA9_GDI_INFO_BASE_Y);

	coda_write(dev, 0, CODA9_REG_JPEG_DPB_BASE00);
	coda_write(dev, 0, CODA9_GDI_CONTROL);
	coda_write(dev, 1, CODA9_GDI_PIC_INIT_HOST);

	trace_coda_jpeg_run(ctx, src_buf);

	coda_write(dev, 1, CODA9_REG_JPEG_PIC_START);

	return 0;
}

static void coda9_jpeg_finish_decode(struct coda_ctx *ctx)
{
	struct coda_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *dst_buf, *src_buf;
	struct coda_q_data *q_data_dst;
	u32 err_mb;

	err_mb = coda_read(dev, CODA9_REG_JPEG_PIC_ERRMB);
	if (err_mb)
		v4l2_err(&dev->v4l2_dev, "ERRMB: 0x%x\n", err_mb);

	coda_write(dev, 0, CODA9_REG_JPEG_BBC_FLUSH_CMD);

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	dst_buf->sequence = ctx->osequence++;

	trace_coda_jpeg_done(ctx, dst_buf);

	dst_buf->flags &= ~(V4L2_BUF_FLAG_PFRAME | V4L2_BUF_FLAG_LAST);
	dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
	dst_buf->flags |= src_buf->flags & V4L2_BUF_FLAG_LAST;

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, false);

	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, q_data_dst->sizeimage);

	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
	coda_m2m_buf_done(ctx, dst_buf, err_mb ? VB2_BUF_STATE_ERROR :
						 VB2_BUF_STATE_DONE);

	coda_dbg(1, ctx, "job finished: decoded frame (%u)%s\n",
		 dst_buf->sequence,
		 (dst_buf->flags & V4L2_BUF_FLAG_LAST) ? " (last)" : "");

	/*
	 * Reset JPEG processing unit after each decode run to work
	 * around hangups when switching context between encoder and
	 * decoder.
	 */
	coda_hw_reset(ctx);
}

const struct coda_context_ops coda9_jpeg_decode_ops = {
	.queue_init = coda_encoder_queue_init, /* non-bitstream operation */
	.start_streaming = coda9_jpeg_start_decoding,
	.prepare_run = coda9_jpeg_prepare_decode,
	.finish_run = coda9_jpeg_finish_decode,
	.release = coda9_jpeg_release,
};

irqreturn_t coda9_jpeg_irq_handler(int irq, void *data)
{
	struct coda_dev *dev = data;
	struct coda_ctx *ctx;
	int status;
	int err_mb;

	WARN_ON(dev == NULL);
	if (dev == NULL)
		return IRQ_HANDLED;

	status = coda_read(dev, CODA9_REG_JPEG_PIC_STATUS);
	if (status == 0)
		return IRQ_HANDLED;
	coda_write(dev, status, CODA9_REG_JPEG_PIC_STATUS);

	if (status & CODA9_JPEG_STATUS_OVERFLOW)
		v4l2_err(&dev->v4l2_dev, "JPEG overflow\n");

	if (status & CODA9_JPEG_STATUS_BBC_INT)
		v4l2_err(&dev->v4l2_dev, "JPEG BBC interrupt\n");

	if (status & CODA9_JPEG_STATUS_ERROR) {
		v4l2_err(&dev->v4l2_dev, "JPEG error\n");

		err_mb = coda_read(dev, CODA9_REG_JPEG_PIC_ERRMB);
		if (err_mb) {
			v4l2_err(&dev->v4l2_dev,
				 "ERRMB: 0x%x: rst idx %d, mcu pos (%d,%d)\n",
				 err_mb, err_mb >> 24, (err_mb >> 12) & 0xfff,
				 err_mb & 0xfff);
		}

v4l2_err(&dev->v4l2_dev,
	 "cur_pos = %d ext_addr = %x int_addr = %d data_cnt = %d\n",
	 coda_read(dev, CODA9_REG_JPEG_BBC_CUR_POS),
	 coda_read(dev, CODA9_REG_JPEG_BBC_EXT_ADDR),
	 coda_read(dev, CODA9_REG_JPEG_BBC_INT_ADDR),
	 coda_read(dev, CODA9_REG_JPEG_BBC_DATA_CNT));
v4l2_err(&dev->v4l2_dev, "word_ptr = %d bit_ptr = %d\n",
	 coda_read(dev, CODA9_REG_JPEG_GBU_WD_PTR),
	 coda_read(dev, CODA9_REG_JPEG_GBU_FF_RPTR));
	}

	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (ctx == NULL) {
		v4l2_err(&dev->v4l2_dev,
			 "Instance released before the end of transaction\n");
		mutex_unlock(&dev->coda_mutex);
		return IRQ_HANDLED;
	}

	coda_stats_done(dev->jpeg_stats);

	complete(&ctx->completion);

	return IRQ_HANDLED;
}
