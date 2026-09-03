#ifndef VPLAYER_AUDIO_H
#define VPLAYER_AUDIO_H

#include <stdint.h>

struct audio_out {
	void *pcm; /* snd_pcm_t*, opaque here to keep alsa/asoundlib.h out of the header */
};

/* Opens the given ALSA device (pass "default" for MiSTer's single shared
 * audio output) at 48kHz stereo S16LE, matching the framework's audio_out.sv
 * alsa mixer input. */
int audio_open(struct audio_out *a, const char *device);
void audio_close(struct audio_out *a);

/* Blocking write of `frames` interleaved stereo S16LE samples. Recovers
 * from underrun automatically. */
int audio_write(struct audio_out *a, const int16_t *samples, int frames);

#endif
