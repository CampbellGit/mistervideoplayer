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

	/* Prefer a generous buffer (500ms) so a slow/CPU-starved video
	 * decode+scale in the same loop doesn't starve the ALSA buffer and
	 * produce audible dropouts -- our decode loop is single-threaded, so
	 * anything that makes one iteration take a while (a heavy video
	 * frame) delays the next audio_write() by the same amount. But the
	 * real hardware device may reject a buffer that large outright
	 * (unlike ALSA's "null" test sink, which accepts anything) -- fall
	 * back to smaller values rather than disabling audio entirely. */
	static const unsigned latencies_us[] = { 500000, 200000, 100000, 50000 };
	rc = -1;
	for (size_t i = 0; i < sizeof(latencies_us) / sizeof(latencies_us[0]); i++) {
		rc = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
			2, 48000, 1, latencies_us[i]);
		if (rc >= 0) {
			fprintf(stderr, "audio: using %ums buffer\n", latencies_us[i] / 1000);
			break;
		}
		fprintf(stderr, "audio: %ums buffer rejected: %s\n", latencies_us[i] / 1000, snd_strerror(rc));
	}
	if (rc < 0) {
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
