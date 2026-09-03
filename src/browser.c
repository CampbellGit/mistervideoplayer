#include "browser.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ENTRIES 512

struct entry {
	char name[256];
	int is_dir;
};

static const char *video_exts[] = {
	"mp4", "mkv", "avi", "mov", "mpg", "mpeg", "m4v", "ts", "wmv",
	"flv", "webm", "ogv", "vob", "3gp", "asf", "m2ts", NULL
};

static int is_video_file(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (!dot)
		return 0;
	for (int i = 0; video_exts[i]; i++)
		if (strcasecmp(dot + 1, video_exts[i]) == 0)
			return 1;
	return 0;
}

static int entry_cmp(const void *pa, const void *pb)
{
	const struct entry *a = pa, *b = pb;
	if (a->is_dir != b->is_dir)
		return b->is_dir - a->is_dir; /* directories first */
	return strcasecmp(a->name, b->name);
}

static int list_dir(const char *path, struct entry *out, int max)
{
	DIR *d = opendir(path);
	if (!d)
		return -1;

	int count = 0;
	struct dirent *de;
	while ((de = readdir(d)) && count < max) {
		/* Skip all dotfiles, not just "."/"..": macOS Finder creates a
		 * "._realname.mp4" AppleDouble metadata sidecar next to every
		 * real file when copying onto a non-Mac filesystem (like this
		 * SD card's FAT32), and it matches our extension filter just
		 * as well as the real file -- selecting one is why "moov atom
		 * not found" showed up on files that were never actually
		 * broken. */
		if (de->d_name[0] == '.')
			continue;

		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", path, de->d_name);

		int is_dir = (de->d_type == DT_DIR);
		if (de->d_type == DT_UNKNOWN) {
			struct stat st;
			if (stat(full, &st) == 0)
				is_dir = S_ISDIR(st.st_mode);
		}

		if (!is_dir && !is_video_file(de->d_name))
			continue;

		strncpy(out[count].name, de->d_name, sizeof(out[count].name) - 1);
		out[count].name[sizeof(out[count].name) - 1] = 0;
		out[count].is_dir = is_dir;
		count++;
	}
	closedir(d);

	qsort(out, count, sizeof(out[0]), entry_cmp);
	return count;
}

/* CRT overscan crops the outer edge of the picture, so a leading "> "
 * marker in column 0 (or content on the very first/last row) can end up
 * invisible. Margin everything a couple of rows/columns in, and make the
 * selected line's highlight the whole line (reverse video) rather than a
 * marker that lives exactly where overscan likes to eat it. */
#define MARGIN_COLS "  "

static void draw(const char *cur_dir, struct entry *entries, int count, int sel)
{
	printf("\x1b[2J\x1b[H\r\n");
	printf(MARGIN_COLS "Video Player -- %s\r\n\r\n", cur_dir);
	for (int i = 0; i < count; i++) {
		char line[300];
		snprintf(line, sizeof(line), "%s%s", entries[i].name, entries[i].is_dir ? "/" : "");
		if (i == sel)
			printf(MARGIN_COLS "\x1b[7m> %-40s\x1b[0m\r\n", line);
		else
			printf(MARGIN_COLS "  %s\r\n", line);
	}
	printf("\r\n" MARGIN_COLS "[up/down] move  [enter] open  [esc] up/quit  [q] quit\r\n\r\n");
	fflush(stdout);
}

int browser_run(const char *root_dir, struct input_state *in,
	char *selected_path, size_t selected_path_size)
{
	char cur_dir[PATH_MAX];
	strncpy(cur_dir, root_dir, sizeof(cur_dir) - 1);
	cur_dir[sizeof(cur_dir) - 1] = 0;

	struct entry entries[MAX_ENTRIES];
	int count = list_dir(cur_dir, entries, MAX_ENTRIES);
	if (count < 0) {
		fprintf(stderr, "browser: can't open %s\n", cur_dir);
		return -1;
	}

	/* Defense in depth: whatever was happening before this call (e.g. a
	 * bounced leftover transition from whatever press just stopped
	 * video playback) shouldn't be misread as the first action here. */
	input_flush(in);

	int sel = 0;
	draw(cur_dir, entries, count, sel);

	for (;;) {
		enum input_action action = input_poll(in);

		if (action == INPUT_NONE) {
			usleep(20000);
			continue;
		}

		fprintf(stderr, "browser: action=%d sel=%d count=%d dir=%s\n", action, sel, count, cur_dir);

		if (action == INPUT_QUIT)
			return -1;

		if (action == INPUT_UP) {
			if (count > 0)
				sel = (sel - 1 + count) % count;
		} else if (action == INPUT_DOWN) {
			if (count > 0)
				sel = (sel + 1) % count;
		} else if (action == INPUT_CANCEL) {
			if (strcmp(cur_dir, root_dir) == 0)
				return -1; /* quit from the top level */
			char *slash = strrchr(cur_dir, '/');
			if (slash && slash != cur_dir)
				*slash = 0;
			else
				cur_dir[1] = 0;
			count = list_dir(cur_dir, entries, MAX_ENTRIES);
			if (count < 0)
				return -1;
			sel = 0;
		} else if (action == INPUT_CONFIRM) {
			if (count == 0)
				continue;

			char next_dir[PATH_MAX];
			snprintf(next_dir, sizeof(next_dir), "%s/%s", cur_dir, entries[sel].name);

			if (entries[sel].is_dir) {
				int next_count = list_dir(next_dir, entries, MAX_ENTRIES);
				if (next_count < 0)
					continue;
				strncpy(cur_dir, next_dir, sizeof(cur_dir) - 1);
				cur_dir[sizeof(cur_dir) - 1] = 0;
				count = next_count;
				sel = 0;
			} else {
				strncpy(selected_path, next_dir, selected_path_size - 1);
				selected_path[selected_path_size - 1] = 0;
				return 0;
			}
		}

		draw(cur_dir, entries, count, sel);

		/* Mechanical switch bounce (common on cheap/worn d-pad
		 * contacts) fires several rapid transitions from one physical
		 * press, all queued within milliseconds of each other. A plain
		 * sleep here doesn't help -- it still processes every queued
		 * transition, just spaced out, which moves the selection
		 * several times per real press. Discard the whole burst after
		 * acting on the first transition instead. */
		input_flush(in);
		usleep(50000);
	}
}
