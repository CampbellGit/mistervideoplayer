#include "audio.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING_SECONDS 3
#define FEED_CHUNK_FRAMES 1024 /* how much the feeder thread hands to ALSA at a time */

static int alsa_write_blocking(snd_pcm_t *pcm, const int16_t *samples, int frames)
{
	while (frames > 0) {
		snd_pcm_sframes_t written = snd_pcm_writei(pcm, samples, frames);
		if (written == -EPIPE) {
			snd_pcm_prepare(pcm);
			continue;
		}
		if (written < 0) {
			int rc = snd_pcm_recover(pcm, (int)written, 1);
			if (rc < 0) {
				fprintf(stderr, "audio: write failed: %s\n", snd_strerror((int)written));
				return -1;
			}
			continue;
		}
		samples += (size_t)written * 2;
		frames -= (int)written;
	}
	return 0;
}

static void *feeder_thread_fn(void *arg)
{
	struct audio_out *a = arg;
	snd_pcm_t *pcm = (snd_pcm_t *)a->pcm;
	int16_t chunk[FEED_CHUNK_FRAMES * 2];

	for (;;) {
		pthread_mutex_lock(&a->mutex);
		while (a->ring_filled == 0 && !a->shutdown)
			pthread_cond_wait(&a->cond, &a->mutex);

		if (a->ring_filled == 0 && a->shutdown) {
			pthread_mutex_unlock(&a->mutex);
			break;
		}

		int n = a->ring_filled < FEED_CHUNK_FRAMES ? a->ring_filled : FEED_CHUNK_FRAMES;
		int first = a->ring_capacity_frames - a->ring_read_pos;
		if (first > n)
			first = n;

		memcpy(chunk, a->ring + (size_t)a->ring_read_pos * 2, (size_t)first * 2 * sizeof(int16_t));
		if (n > first)
			memcpy(chunk + (size_t)first * 2, a->ring, (size_t)(n - first) * 2 * sizeof(int16_t));

		a->ring_read_pos = (a->ring_read_pos + n) % a->ring_capacity_frames;
		a->ring_filled -= n;
		pthread_cond_broadcast(&a->cond); /* wake a producer waiting on room */
		pthread_mutex_unlock(&a->mutex);

		alsa_write_blocking(pcm, chunk, n);
	}

	return NULL;
}

int audio_open(struct audio_out *a, const char *device)
{
	memset(a, 0, sizeof(*a));

	snd_pcm_t *pcm;
	int rc = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
	if (rc < 0) {
		fprintf(stderr, "audio: open %s: %s\n", device, snd_strerror(rc));
		return -1;
	}

	/* Real hardware may reject a large buffer outright (seen: 500ms and
	 * 200ms both rejected, 100ms accepted) -- fall back to smaller sizes
	 * rather than disabling audio entirely. This no longer needs to be
	 * generous, since the ring buffer above is what actually absorbs
	 * video-decode stalls now. */
	static const unsigned latencies_us[] = { 200000, 100000, 50000, 20000 };
	rc = -1;
	for (size_t i = 0; i < sizeof(latencies_us) / sizeof(latencies_us[0]); i++) {
		rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
			2, 48000, 1, latencies_us[i]);
		if (rc >= 0) {
			fprintf(stderr, "audio: using %ums hw buffer\n", latencies_us[i] / 1000);
			break;
		}
		fprintf(stderr, "audio: %ums hw buffer rejected: %s\n", latencies_us[i] / 1000, snd_strerror(rc));
	}
	if (rc < 0) {
		snd_pcm_close(pcm);
		return -1;
	}

	a->pcm = pcm;
	a->ring_capacity_frames = 48000 * RING_SECONDS;
	a->ring = malloc((size_t)a->ring_capacity_frames * 2 * sizeof(int16_t));
	if (!a->ring) {
		snd_pcm_close(pcm);
		return -1;
	}

	pthread_mutex_init(&a->mutex, NULL);
	pthread_cond_init(&a->cond, NULL);

	if (pthread_create(&a->thread, NULL, feeder_thread_fn, a) != 0) {
		fprintf(stderr, "audio: failed to start feeder thread\n");
		free(a->ring);
		snd_pcm_close(pcm);
		return -1;
	}

	return 0;
}

void audio_close(struct audio_out *a)
{
	if (!a->pcm)
		return;

	pthread_mutex_lock(&a->mutex);
	a->shutdown = 1;
	pthread_cond_broadcast(&a->cond);
	pthread_mutex_unlock(&a->mutex);

	pthread_join(a->thread, NULL);
	pthread_mutex_destroy(&a->mutex);
	pthread_cond_destroy(&a->cond);

	snd_pcm_drain((snd_pcm_t *)a->pcm);
	snd_pcm_close((snd_pcm_t *)a->pcm);
	free(a->ring);
	memset(a, 0, sizeof(*a));
}

int audio_write(struct audio_out *a, const int16_t *samples, int frames)
{
	pthread_mutex_lock(&a->mutex);

	while (a->ring_capacity_frames - a->ring_filled < frames && !a->shutdown)
		pthread_cond_wait(&a->cond, &a->mutex);

	if (a->shutdown) {
		pthread_mutex_unlock(&a->mutex);
		return -1;
	}

	int first = a->ring_capacity_frames - a->ring_write_pos;
	if (first > frames)
		first = frames;

	memcpy(a->ring + (size_t)a->ring_write_pos * 2, samples, (size_t)first * 2 * sizeof(int16_t));
	if (frames > first)
		memcpy(a->ring, samples + (size_t)first * 2, (size_t)(frames - first) * 2 * sizeof(int16_t));

	a->ring_write_pos = (a->ring_write_pos + frames) % a->ring_capacity_frames;
	a->ring_filled += frames;

	pthread_cond_broadcast(&a->cond); /* wake the feeder if it was waiting on data */
	pthread_mutex_unlock(&a->mutex);

	return frames;
}

void audio_flush(struct audio_out *a)
{
	pthread_mutex_lock(&a->mutex);
	a->ring_write_pos = 0;
	a->ring_read_pos = 0;
	a->ring_filled = 0;
	pthread_cond_broadcast(&a->cond); /* wake a producer that was blocked waiting on room */
	pthread_mutex_unlock(&a->mutex);
}
