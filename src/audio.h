#ifndef VPLAYER_AUDIO_H
#define VPLAYER_AUDIO_H

#include <pthread.h>
#include <stdint.h>

/* Real hardware ALSA buffers on MiSTer can be as small as ~100ms (measured:
 * anything above that gets rejected outright), and our decode loop is
 * single-threaded -- a slow video frame delays the next audio_write() by
 * however long it took. A 100ms hardware buffer alone can't absorb that, so
 * audio actually plays back on a dedicated thread fed through a much
 * larger ring buffer we own; the decode loop only has to keep that ring
 * topped up, not hit ALSA's tight deadline directly. */
struct audio_out {
	void *pcm; /* snd_pcm_t*, opaque here to keep alsa/asoundlib.h out of the header */

	int16_t *ring;
	int ring_capacity_frames; /* stereo frames */
	int ring_write_pos;
	int ring_read_pos;
	int ring_filled;

	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int shutdown;
};

int audio_open(struct audio_out *a, const char *device);
void audio_close(struct audio_out *a);

/* Copies `frames` interleaved stereo S16LE samples into the ring buffer,
 * blocking only if the ring itself is full (which under normal playback
 * shouldn't happen -- it's sized for several seconds of headroom). The
 * feeder thread does the actual (potentially blocking) ALSA write. */
int audio_write(struct audio_out *a, const int16_t *samples, int frames);

/* Discards whatever's currently queued in the ring buffer -- call this
 * right after a seek, so stale pre-seek audio doesn't keep playing out
 * of order. Does not touch whatever ALSA's own tiny hardware buffer is
 * already mid-playing (at most ~100ms on this platform); that's a minor,
 * acceptable blip rather than worth the complexity/risk of reaching into
 * the ALSA handle from a thread other than the feeder. */
void audio_flush(struct audio_out *a);

#endif
