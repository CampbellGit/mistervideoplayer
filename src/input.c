#include "input.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int input_open_all(struct input_state *in)
{
	in->count = 0;

	DIR *d = opendir("/dev/input");
	if (!d) {
		perror("input: opendir /dev/input");
		return -1;
	}

	struct dirent *de;
	while ((de = readdir(d)) && in->count < INPUT_MAX_DEVICES) {
		if (strncmp(de->d_name, "event", 5) != 0)
			continue;

		char path[320];
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);

		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;

		in->hat_x[in->count] = 0;
		in->hat_y[in->count] = 0;
		in->fds[in->count++] = fd;
	}
	closedir(d);

	return in->count > 0 ? 0 : -1;
}

void input_close_all(struct input_state *in)
{
	for (int i = 0; i < in->count; i++)
		close(in->fds[i]);
	in->count = 0;
}

static enum input_action map_keycode(int code)
{
	switch (code) {
	case KEY_UP:        return INPUT_UP;
	case KEY_DOWN:      return INPUT_DOWN;
	case KEY_LEFT:      return INPUT_LEFT;
	case KEY_RIGHT:     return INPUT_RIGHT;
	case KEY_ENTER:
	case KEY_SPACE:     return INPUT_CONFIRM;
	case KEY_ESC:
	case KEY_BACKSPACE: return INPUT_CANCEL;
	case KEY_P:         return INPUT_PAUSE;
	case KEY_Q:         return INPUT_QUIT;

	/* Standard Linux gamepad codes (BTN_DPAD_* on pads whose driver
	 * reports the d-pad as buttons rather than a hat; BTN_SOUTH/EAST
	 * are the bottom/right face buttons in the kernel's abstract
	 * gamepad mapping -- "confirm"/"back" on most controllers). */
	case BTN_DPAD_UP:    return INPUT_UP;
	case BTN_DPAD_DOWN:  return INPUT_DOWN;
	case BTN_DPAD_LEFT:  return INPUT_LEFT;
	case BTN_DPAD_RIGHT: return INPUT_RIGHT;
	case BTN_SOUTH:      return INPUT_CONFIRM;
	case BTN_EAST:       return INPUT_CANCEL;
	case BTN_START:      return INPUT_PAUSE;

	default:            return INPUT_NONE;
	}
}

/* Many pads report the d-pad as a hat axis instead of BTN_DPAD_*
 * buttons. Fire once on the transition away from center (0) so a held
 * direction doesn't flood one action per poll. */
static enum input_action map_hat_edge(int prev, int cur, enum input_action neg, enum input_action pos)
{
	if (cur == prev || cur == 0)
		return INPUT_NONE;
	return cur < 0 ? neg : pos;
}

void input_flush(struct input_state *in)
{
	struct input_event ev;

	for (int i = 0; i < in->count; i++) {
		while (read(in->fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
			; /* discard */
		in->hat_x[i] = 0;
		in->hat_y[i] = 0;
	}
}

enum input_action input_poll(struct input_state *in)
{
	struct input_event ev;

	for (int i = 0; i < in->count; i++) {
		ssize_t n = read(in->fds[i], &ev, sizeof(ev));
		if (n != (ssize_t)sizeof(ev))
			continue;

		if (ev.type == EV_KEY && ev.value == 1) { /* key/button-down only */
			enum input_action action = map_keycode(ev.code);
			if (action != INPUT_NONE)
				return action;
		} else if (ev.type == EV_ABS && ev.code == ABS_HAT0X) {
			enum input_action action = map_hat_edge(in->hat_x[i], ev.value, INPUT_LEFT, INPUT_RIGHT);
			in->hat_x[i] = ev.value;
			if (action != INPUT_NONE)
				return action;
		} else if (ev.type == EV_ABS && ev.code == ABS_HAT0Y) {
			enum input_action action = map_hat_edge(in->hat_y[i], ev.value, INPUT_UP, INPUT_DOWN);
			in->hat_y[i] = ev.value;
			if (action != INPUT_NONE)
				return action;
		}
	}

	return INPUT_NONE;
}
