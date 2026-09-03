#ifndef VPLAYER_DECODE_H
#define VPLAYER_DECODE_H

#include <stdint.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

enum decode_result {
	DECODE_EOF = 0,
	DECODE_VIDEO,
	DECODE_AUDIO,
	DECODE_AGAIN, /* packet consumed, nothing to present yet */
};

struct decoder {
	AVFormatContext *fmt;
	int v_idx;
	int a_idx;
	AVCodecContext *vctx;
	AVCodecContext *actx;
	struct SwsContext *sws;
	struct SwrContext *swr;
	AVFrame *frame;
	AVPacket *pkt;

	int sws_dst_w;
	int sws_dst_h;
	int sws_dst_stride;

	double video_pts_s;
	double audio_pts_s;
};

int decoder_open(struct decoder *d, const char *path);
void decoder_close(struct decoder *d);

static inline int decoder_has_video(struct decoder *d) { return d->v_idx >= 0; }
static inline int decoder_has_audio(struct decoder *d) { return d->a_idx >= 0; }
int decoder_video_width(struct decoder *d);
int decoder_video_height(struct decoder *d);

/* When we're badly behind schedule, skip fully decoding non-reference
 * frames (typically B-frames) instead of paying their full decode cost
 * only to then drop them at the presentation stage anyway. Cheap safety
 * valve, not a fix for a source that's fundamentally too heavy for this
 * CPU -- on a real bitrate gap (source demands ~10x realtime), this buys
 * some percent, not an order of magnitude. */
void decoder_set_fast_mode(struct decoder *d, int enable);

/* Reads/decodes until it has something to present. The result tells the
 * caller whether to call decoder_scale_video() or decoder_resample_audio()
 * next; DECODE_AGAIN means "call decoder_step() again", DECODE_EOF means
 * the file is exhausted. */
enum decode_result decoder_step(struct decoder *d);

/* Scales the most recently decoded video frame into dst (dst_fmt must be a
 * 32bpp packed format understood by libswscale, e.g. AV_PIX_FMT_BGR0 to
 * match MiSTer's fb0 BGRX8888 layout). Safe to call with a different
 * dst_w/dst_h each time (e.g. after a display mode change). */
int decoder_scale_video(struct decoder *d, uint8_t *dst, int dst_w, int dst_h,
	int dst_stride, enum AVPixelFormat dst_fmt);

/* Resamples the most recently decoded audio frame to 48kHz stereo S16LE
 * into dst (caller-sized for at least decoder_max_audio_frames() frames).
 * Returns the number of stereo frames written. */
int decoder_resample_audio(struct decoder *d, int16_t *dst, int max_frames);
int decoder_max_audio_frames(struct decoder *d);

/* Seeks to the nearest keyframe at or before target_seconds (clamped to
 * >= 0) and flushes decoder state. Caller still needs to discard/rebase
 * anything it tracks about "current position" itself (queued audio,
 * wall-clock pacing baseline for video-only files, etc). */
int decoder_seek(struct decoder *d, double target_seconds);

#endif
