#ifndef VPLAYER_FB_H
#define VPLAYER_FB_H

#include <stdint.h>
#include <stddef.h>

/* Thin wrapper around a Linux framebuffer device (MiSTer's /dev/fb0, which
 * Main_MiSTer already configures as 32bpp BGRX8888 before our Scripts-menu
 * launcher gets control). Falls back to a plain-file mock (sized via
 * VPLAYER_FB_WIDTH/VPLAYER_FB_HEIGHT env vars) when the target path isn't a
 * real framebuffer device, so the drawing code can be exercised on a
 * desktop without MiSTer hardware. */
struct fb_dev {
	int fd;
	uint8_t *mem;
	size_t mem_size;
	int width;
	int height;
	int stride;   /* bytes per line */
	int bpp;      /* bits per pixel */
	int is_mock;
};

int fb_open(struct fb_dev *fb, const char *path);
void fb_close(struct fb_dev *fb);

/* Blocks until the next vertical sync. No-op on a mock fb. */
void fb_wait_vsync(struct fb_dev *fb);

/* Pointer to the start of pixel row y (0-based). */
static inline uint8_t *fb_row(struct fb_dev *fb, int y)
{
	return fb->mem + (size_t)y * fb->stride;
}

#endif
