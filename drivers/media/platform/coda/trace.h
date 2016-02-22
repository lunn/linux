/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM coda

#if !defined(__CODA_TRACE_H__) || defined(TRACE_HEADER_MULTI_READ)
#define __CODA_TRACE_H__

#include <linux/tracepoint.h>
#include <media/videobuf2-v4l2.h>

#include "coda.h"

TRACE_EVENT(coda_bit_run,
	TP_PROTO(struct coda_ctx *ctx, int cmd),

	TP_ARGS(ctx, cmd),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, ctx)
		__field(int, cmd)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->ctx = ctx->idx;
		__entry->cmd = cmd;
	),

	TP_printk("minor = %d, ctx = %d, cmd = %d",
		  __entry->minor, __entry->ctx, __entry->cmd)
);

TRACE_EVENT(coda_bit_done,
	TP_PROTO(struct coda_ctx *ctx),

	TP_ARGS(ctx),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, ctx = %d", __entry->minor, __entry->ctx)
);

DECLARE_EVENT_CLASS(coda_buf_class,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),

	TP_ARGS(ctx, buf),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, index)
		__field(int, ctx)
		__field(dma_addr_t, paddr)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->index = buf->vb2_buf.index;
		__entry->ctx = ctx->idx;
		__entry->paddr = vb2_dma_contig_plane_dma_addr(&buf->vb2_buf, 0);
	),

	TP_printk("minor = %d, index = %d, ctx = %d, paddr = %pad",
		  __entry->minor, __entry->index, __entry->ctx, &__entry->paddr)
);

DEFINE_EVENT(coda_buf_class, coda_enc_pic_run,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),
	TP_ARGS(ctx, buf)
);

DEFINE_EVENT(coda_buf_class, coda_enc_pic_done,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),
	TP_ARGS(ctx, buf)
);

DECLARE_EVENT_CLASS(coda_buf_meta_class,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		 struct coda_buffer_meta *meta),

	TP_ARGS(ctx, buf, meta),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, index)
		__field(s64, timestamp)
		__field(int, start)
		__field(int, end)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->index = buf->vb2_buf.index;
		__entry->timestamp = buf->vb2_buf.timestamp;
		__entry->start = meta->start & ctx->bitstream_fifo.kfifo.mask;
		__entry->end = meta->end & ctx->bitstream_fifo.kfifo.mask;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, index = %d, timestamp = %lld, start = 0x%x, end = 0x%x, ctx = %d",
		  __entry->minor, __entry->index, __entry->timestamp,
		   __entry->start, __entry->end, __entry->ctx)
);

DEFINE_EVENT(coda_buf_meta_class, coda_bit_queue,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		 struct coda_buffer_meta *meta),
	TP_ARGS(ctx, buf, meta)
);

DECLARE_EVENT_CLASS(coda_meta_class,
	TP_PROTO(struct coda_ctx *ctx, struct coda_buffer_meta *meta),

	TP_ARGS(ctx, meta),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(int, start)
		__field(int, end)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->start = meta ? (meta->start &
					 ctx->bitstream_fifo.kfifo.mask) : 0;
		__entry->end = meta ? (meta->end &
				       ctx->bitstream_fifo.kfifo.mask) : 0;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, start = 0x%x, end = 0x%x, ctx = %d",
		  __entry->minor, __entry->start, __entry->end, __entry->ctx)
);

DEFINE_EVENT(coda_meta_class, coda_dec_pic_run,
	TP_PROTO(struct coda_ctx *ctx, struct coda_buffer_meta *meta),
	TP_ARGS(ctx, meta)
);

DEFINE_EVENT(coda_meta_class, coda_dec_pic_done,
	TP_PROTO(struct coda_ctx *ctx, struct coda_buffer_meta *meta),
	TP_ARGS(ctx, meta)
);

DEFINE_EVENT(coda_buf_meta_class, coda_dec_rot_done,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf,
		 struct coda_buffer_meta *meta),
	TP_ARGS(ctx, buf, meta)
);

TRACE_EVENT(coda_not_ready,
	TP_PROTO(struct coda_ctx *ctx, bool stream_end, int src_bufs,
		 int num_metas, int payload),

	TP_ARGS(ctx, stream_end, src_bufs, num_metas, payload),

	TP_STRUCT__entry(
		__field(int, minor)
		__field(bool, hold)
		__field(bool, stream_end)
		__field(int, src_bufs)
		__field(int, num_metas)
		__field(int, payload)
		__field(int, ctx)
	),

	TP_fast_assign(
		__entry->minor = ctx->fh.vdev->minor;
		__entry->hold = ctx->hold;
		__entry->stream_end = stream_end;
		__entry->src_bufs = src_bufs;
		__entry->num_metas = num_metas;
		__entry->payload = payload;
		__entry->ctx = ctx->idx;
	),

	TP_printk("minor = %d, hold = %d, stream_end = %d, src_bufs = %d, metas = %d, payload = %d, ctx = %d",
		  __entry->minor, __entry->hold, __entry->stream_end,
		  __entry->src_bufs, __entry->num_metas, __entry->payload,
		  __entry->ctx)
);

DEFINE_EVENT(coda_buf_class, coda_jpeg_run,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),
	TP_ARGS(ctx, buf)
);

DEFINE_EVENT(coda_buf_class, coda_jpeg_done,
	TP_PROTO(struct coda_ctx *ctx, struct vb2_v4l2_buffer *buf),
	TP_ARGS(ctx, buf)
);

#endif /* __CODA_TRACE_H__ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/media/platform/coda
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
