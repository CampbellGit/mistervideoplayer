#ifndef VPLAYER_INPUT_H
#define VPLAYER_INPUT_H

#define INPUT_MAX_DEVICES 32

enum input_action {
	INPUT_NONE = 0,
	INPUT_UP,
	INPUT_DOWN,
	INPUT_LEFT,
	INPUT_RIGHT,
	INPUT_CONFIRM,
	INPUT_CANCEL,
	INPUT_PAUSE,
	INPUT_QUIT,
};

struct input_state {
	int fds[INPUT_MAX_DEVICES];
	int hat_x[INPUT_MAX_DEVICES]; /* last seen ABS_HAT0X value, for edge detection */
	int hat_y[INPUT_MAX_DEVICES]; /* last seen ABS_HAT0Y value */
	int count;
};

/* Opens every readable /dev/input/event* device it can (evdev). MiSTer
 * exposes real controllers/keyboards this way; we don't special-case any
 * particular pad. We listen for standard keyboard arrow/enter/esc codes
 * *and* the standard Linux gamepad codes (BTN_SOUTH/BTN_EAST for
 * confirm/cancel, ABS_HAT0X/ABS_HAT0Y for the d-pad) on all of them. */
int input_open_all(struct input_state *in);
void input_close_all(struct input_state *in);

/* Non-blocking: returns the next mapped action, or INPUT_NONE if nothing
 * is pending right now. */
enum input_action input_poll(struct input_state *in);

#endif
