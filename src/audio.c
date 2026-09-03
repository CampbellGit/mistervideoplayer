#include "audio.h"

#include <alsa/asoundlib.h>
#include <stdio.h>

int audio_open(struct audio_out *a, const char *device)
{
	snd_pcm_t *pcm;
	int rc = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
	if (rc < 0) {
		fprintf(stderr, "audio: open %s: %s\n", device, snd_strerror(rc));
		return -1;
	}

	/* Generous buffer (500ms) so a slow/CPU-starved video decode+scale in
	 * the same loop doesn't starve the ALSA buffer and produce audible
	 * dropouts -- our decode loop is single-threaded, so anything that
	 * makes one iteration take a while (a heavy video frame) delays the
	 * next audio_write() by the same amount. */
	rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
		2, 48000, 1, 500000);
	if (rc < 0) {
		fprintf(stderr, "audio: set_params: %s\n", snd_strerror(rc));
		snd_pcm_close(pcm);
		return -1;
	}

	a->pcm = pcm;
	return 0;
}

void audio_close(struct audio_out *a)
{
	if (a->pcm) {
		snd_pcm_drain((snd_pcm_t *)a->pcm);
		snd_pcm_close((snd_pcm_t *)a->pcm);
		a->pcm = NULL;
	}
}

int audio_write(struct audio_out *a, const int16_t *samples, int frames)
{
	snd_pcm_t *pcm = (snd_pcm_t *)a->pcm;
	snd_pcm_sframes_t written = snd_pcm_writei(pcm, samples, frames);

	if (written == -EPIPE) {
		snd_pcm_prepare(pcm);
		written = snd_pcm_writei(pcm, samples, frames);
	} else if (written < 0) {
		int rc = snd_pcm_recover(pcm, (int)written, 1);
		if (rc < 0) {
			fprintf(stderr, "audio: write failed: %s\n", snd_strerror((int)written));
			return -1;
		}
		return 0;
	}

	return (int)written;
}
