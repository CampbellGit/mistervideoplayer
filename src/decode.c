#include "decode.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define AUDIO_FRAME_CAPACITY 8192

static int open_stream(AVFormatContext *fmt, enum AVMediaType type, AVCodecContext **ctx_out)
{
	AVCodec *codec = NULL;
	int idx = av_find_best_stream(fmt, type, -1, -1, &codec, 0);
	if (idx < 0)
		return -1;

	AVCodecContext *ctx = avcodec_alloc_context3(codec);
	if (!ctx)
		return -1;

	if (avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar) < 0) {
		avcodec_free_context(&ctx);
		return -1;
	}

	if (type == AVMEDIA_TYPE_VIDEO) {
		/* Decode was only ever using one CPU core -- the MiSTer's
		 * ARM chip is dual-core, so this leaves half the available
		 * decode throughput completely unused. Most video codecs
		 * support slice/frame-parallel decode; let ffmpeg use it. */
		long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
		ctx->thread_count = (ncpu >= 1 && ncpu <= 8) ? (int)ncpu : 2;
		ctx->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
	}

	if (avcodec_open2(ctx, codec, NULL) < 0) {
		avcodec_free_context(&ctx);
		return -1;
	}

	*ctx_out = ctx;
	return idx;
}

int decoder_open(struct decoder *d, const char *path)
{
	memset(d, 0, sizeof(*d));
	d->v_idx = -1;
	d->a_idx = -1;

	if (avformat_open_input(&d->fmt, path, NULL, NULL) < 0) {
		fprintf(stderr, "decode: can't open %s\n", path);
		return -1;
	}
	if (avformat_find_stream_info(d->fmt, NULL) < 0) {
		fprintf(stderr, "decode: can't find stream info for %s\n", path);
		avformat_close_input(&d->fmt);
		return -1;
	}

	d->v_idx = open_stream(d->fmt, AVMEDIA_TYPE_VIDEO, &d->vctx);
	d->a_idx = open_stream(d->fmt, AVMEDIA_TYPE_AUDIO, &d->actx);

	if (d->v_idx < 0 && d->a_idx < 0) {
		fprintf(stderr, "decode: no playable video or audio stream in %s\n", path);
		decoder_close(d);
		return -1;
	}

	d->frame = av_frame_alloc();
	d->pkt = av_packet_alloc();
	if (!d->frame || !d->pkt) {
		decoder_close(d);
		return -1;
	}

	return 0;
}

void decoder_close(struct decoder *d)
{
	if (d->sws)
		sws_freeContext(d->sws);
	if (d->swr)
		swr_free(&d->swr);
	if (d->pkt)
		av_packet_free(&d->pkt);
	if (d->frame)
		av_frame_free(&d->frame);
	if (d->vctx)
		avcodec_free_context(&d->vctx);
	if (d->actx)
		avcodec_free_context(&d->actx);
	if (d->fmt)
		avformat_close_input(&d->fmt);
}

int decoder_video_width(struct decoder *d) { return d->vctx ? d->vctx->width : 0; }
int decoder_video_height(struct decoder *d) { return d->vctx ? d->vctx->height : 0; }

void decoder_set_fast_mode(struct decoder *d, int enable)
{
	if (d->vctx)
		d->vctx->skip_frame = enable ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
}

static double pts_to_seconds(AVFormatContext *fmt, int stream_idx, int64_t pts)
{
	if (pts == AV_NOPTS_VALUE)
		return -1.0;
	return pts * av_q2d(fmt->streams[stream_idx]->time_base);
}

enum decode_result decoder_step(struct decoder *d)
{
	int rc = av_read_frame(d->fmt, d->pkt);
	if (rc < 0)
		return DECODE_EOF;

	if (d->pkt->stream_index == d->v_idx) {
		rc = avcodec_send_packet(d->vctx, d->pkt);
		av_packet_unref(d->pkt);
		if (rc < 0)
			return DECODE_AGAIN;

		rc = avcodec_receive_frame(d->vctx, d->frame);
		if (rc < 0)
			return DECODE_AGAIN;

		d->video_pts_s = pts_to_seconds(d->fmt, d->v_idx, d->frame->pts);
		return DECODE_VIDEO;
	}

	if (d->pkt->stream_index == d->a_idx) {
		rc = avcodec_send_packet(d->actx, d->pkt);
		av_packet_unref(d->pkt);
		if (rc < 0)
			return DECODE_AGAIN;

		rc = avcodec_receive_frame(d->actx, d->frame);
		if (rc < 0)
			return DECODE_AGAIN;

		d->audio_pts_s = pts_to_seconds(d->fmt, d->a_idx, d->frame->pts);
		return DECODE_AUDIO;
	}

	av_packet_unref(d->pkt);
	return DECODE_AGAIN;
}

int decoder_scale_video(struct decoder *d, uint8_t *dst, int dst_w, int dst_h,
	int dst_stride, enum AVPixelFormat dst_fmt)
{
	if (!d->sws || d->sws_dst_w != dst_w || d->sws_dst_h != dst_h || d->sws_dst_stride != dst_stride) {
		if (d->sws)
			sws_freeContext(d->sws);
		d->sws = sws_getContext(d->vctx->width, d->vctx->height, d->vctx->pix_fmt,
			dst_w, dst_h, dst_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
		d->sws_dst_w = dst_w;
		d->sws_dst_h = dst_h;
		d->sws_dst_stride = dst_stride;
	}
	if (!d->sws)
		return -1;

	uint8_t *dst_planes[4] = { dst, NULL, NULL, NULL };
	int dst_linesize[4] = { dst_stride, 0, 0, 0 };

	sws_scale(d->sws, (const uint8_t *const *)d->frame->data, d->frame->linesize,
		0, d->vctx->height, dst_planes, dst_linesize);
	return 0;
}

int decoder_max_audio_frames(struct decoder *d)
{
	(void)d;
	return AUDIO_FRAME_CAPACITY;
}

int decoder_resample_audio(struct decoder *d, int16_t *dst, int max_frames)
{
	if (!d->swr) {
		int64_t in_layout = d->frame->channel_layout ?
			(int64_t)d->frame->channel_layout :
			av_get_default_channel_layout(d->actx->channels);

		d->swr = swr_alloc_set_opts(NULL,
			AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 48000,
			in_layout, d->actx->sample_fmt, d->actx->sample_rate,
			0, NULL);
		if (!d->swr || swr_init(d->swr) < 0)
			return -1;
	}

	if (max_frames > AUDIO_FRAME_CAPACITY)
		max_frames = AUDIO_FRAME_CAPACITY;

	uint8_t *out_planes[1] = { (uint8_t *)dst };
	int converted = swr_convert(d->swr, out_planes, max_frames,
		(const uint8_t **)d->frame->data, d->frame->nb_samples);
	return converted < 0 ? -1 : converted;
}

int decoder_seek(struct decoder *d, double target_seconds)
{
	if (target_seconds < 0)
		target_seconds = 0;

	int64_t ts = (int64_t)(target_seconds * AV_TIME_BASE);
	if (av_seek_frame(d->fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
		return -1;

	if (d->vctx)
		avcodec_flush_buffers(d->vctx);
	if (d->actx)
		avcodec_flush_buffers(d->actx);

	d->video_pts_s = -1;
	d->audio_pts_s = -1;
	return 0;
}
