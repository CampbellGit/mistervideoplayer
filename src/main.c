#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <libavutil/pixfmt.h>

#include "audio.h"
#include "browser.h"
#include "decode.h"
#include "fb.h"
#include "input.h"

#define AV_SYNC_THRESHOLD_S 0.02
#define AV_DROP_THRESHOLD_S 0.10

static double monotonic_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_seconds(double s)
{
	if (s <= 0)
		return;
	struct timespec ts;
	ts.tv_sec = (time_t)s;
	ts.tv_nsec = (long)((s - ts.tv_sec) * 1e9);
	nanosleep(&ts, NULL);
}

static void play_file(const char *path, struct fb_dev *fb, struct input_state *in)
{
	struct decoder dec;
	if (decoder_open(&dec, path) < 0) {
		fprintf(stderr, "main: failed to open %s\n", path);
		sleep_seconds(1.5);
		return;
	}

	struct audio_out audio = { 0 };
	int file_has_audio_track = decoder_has_audio(&dec);
	int have_audio = file_has_audio_track &&
		audio_open(&audio, getenv("VPLAYER_AUDIO_DEVICE") ? getenv("VPLAYER_AUDIO_DEVICE") : "default") == 0;

	int16_t audio_buf[8192 * 2];
	double audio_clock_s = 0.0;   /* estimated current playback position from audio */
	double play_start_wall = -1;  /* wall clock at first video frame, for video-only files */
	double video_pts_base = 0.0;
	int paused = 0;

	/* The button that just confirmed this file in the browser can leave
	 * a duplicate/leftover transition queued up; without this, that
	 * event gets read as an immediate Cancel on the very first loop
	 * iteration below, exiting before anything is seen or heard. */
	input_flush(in);

	printf("\x1b[2J\x1b[Hplaying %s (audio=%s)\r\n", path, have_audio ? "yes" : "no");
	fprintf(stderr, "main: playing %s -- file_has_audio_track=%d audio_open_ok=%d\n",
		path, file_has_audio_track, have_audio);

	long steps = 0;
	for (;;) {
		enum input_action action = input_poll(in);
		if (action == INPUT_QUIT || action == INPUT_CANCEL) {
			fprintf(stderr, "main: stopping -- input action=%d after %ld decode steps\n", action, steps);
			break;
		}
		if (action == INPUT_PAUSE)
			paused = !paused;
		if (paused) {
			usleep(20000);
			continue;
		}

		enum decode_result rc = decoder_step(&dec);
		steps++;
		if (rc == DECODE_EOF) {
			fprintf(stderr, "main: stopping -- decoder EOF after %ld decode steps\n", steps);
			break;
		}
		if (rc == DECODE_AGAIN)
			continue;

		if (rc == DECODE_AUDIO) {
			int frames = decoder_resample_audio(&dec, audio_buf, decoder_max_audio_frames(&dec));
			if (frames > 0) {
				if (have_audio)
					audio_write(&audio, audio_buf, frames); /* blocks ~realtime, paces playback */
				if (dec.audio_pts_s >= 0)
					audio_clock_s = dec.audio_pts_s;
			}
			continue;
		}

		/* rc == DECODE_VIDEO */
		double target_s = dec.video_pts_s;
		double delta;

		if (have_audio) {
			delta = target_s - audio_clock_s;
		} else {
			if (play_start_wall < 0) {
				play_start_wall = monotonic_now();
				video_pts_base = target_s;
			}
			double expected_pts_now = video_pts_base + (monotonic_now() - play_start_wall);
			delta = target_s - expected_pts_now;
		}

		if (delta > AV_DROP_THRESHOLD_S) {
			/* way ahead of audio/clock: wait instead of dropping, avoids
			 * a stutter when audio itself is briefly starved */
			sleep_seconds(delta);
		} else if (delta < -AV_DROP_THRESHOLD_S) {
			/* way behind: skip presenting this frame to catch back up */
			continue;
		} else if (delta > AV_SYNC_THRESHOLD_S) {
			sleep_seconds(delta);
		}

		decoder_scale_video(&dec, fb_row(fb, 0), fb->width, fb->height, fb->stride, AV_PIX_FMT_BGR0);
		fb_wait_vsync(fb);
	}

	if (have_audio)
		audio_close(&audio);
	decoder_close(&dec);
}

static void redirect_stderr_to_log(void)
{
	/* Once we're drawing real video frames into /dev/fb0, anything we
	 * print to the tty gets overwritten by the next frame almost
	 * immediately -- there's no way to actually read diagnostics on
	 * screen. Send stderr to a persistent log file instead so failures
	 * (audio device busy, decode errors, etc.) are inspectable after
	 * the fact over SSH. */
	const char *log_path = getenv("VPLAYER_LOG");
	if (!log_path)
		log_path = "/media/fat/video-player/vplayer.log";

	FILE *log = fopen(log_path, "a");
	if (!log)
		return; /* stderr just stays on the tty, no harm done */

	dup2(fileno(log), STDERR_FILENO);
	fclose(log);
	setvbuf(stderr, NULL, _IOLBF, 0); /* line-buffered so entries land promptly */
	fprintf(stderr, "\n--- vplayer starting ---\n");
}

int main(int argc, char **argv)
{
	redirect_stderr_to_log();

	const char *target = argc > 1 ? argv[1] : "/media/fat/video";
	const char *fb_path = getenv("VPLAYER_FB_DEVICE");
	if (!fb_path)
		fb_path = "/dev/fb0";

	struct stat st;
	int target_is_file = (stat(target, &st) == 0 && S_ISREG(st.st_mode));

	struct fb_dev fb;
	if (fb_open(&fb, fb_path) < 0) {
		fprintf(stderr, "main: could not open framebuffer %s\n", fb_path);
		return 1;
	}
	if (fb.bpp != 32) {
		fprintf(stderr, "main: framebuffer is %dbpp, only 32bpp (BGRX8888) is supported\n", fb.bpp);
		fb_close(&fb);
		return 1;
	}

	struct input_state in;
	if (input_open_all(&in) < 0)
		fprintf(stderr, "main: no input devices found, controls unavailable\n");

	if (target_is_file) {
		/* Direct invocation with a file path -- skip the browser. Also
		 * how this gets exercised without an interactive input device. */
		play_file(target, &fb, &in);
	} else {
		char selected[1024];
		while (browser_run(target, &in, selected, sizeof(selected)) == 0)
			play_file(selected, &fb, &in);
	}

	input_close_all(&in);
	fb_close(&fb);
	return 0;
}
