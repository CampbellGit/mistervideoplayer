#ifndef VPLAYER_BROWSER_H
#define VPLAYER_BROWSER_H

#include <stddef.h>

#include "input.h"

/* Simple text-mode directory browser, drawn to stdout (we run on a real
 * Linux VT via the Scripts-menu tty2 mechanism, same as `less`/pdfviewer,
 * so plain ANSI escapes + printf are enough -- no need to touch the pixel
 * framebuffer just to list files).
 *
 * Returns 0 and fills selected_path when the user picks a file, or -1 if
 * they quit out of the browser entirely. */
int browser_run(const char *root_dir, struct input_state *in,
	char *selected_path, size_t selected_path_size);

#endif
