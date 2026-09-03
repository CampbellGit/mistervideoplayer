#include "fb.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int open_mock(struct fb_dev *fb, const char *path)
{
	const char *w_env = getenv("VPLAYER_FB_WIDTH");
	const char *h_env = getenv("VPLAYER_FB_HEIGHT");
	if (!w_env || !h_env) {
		fprintf(stderr, "fb: %s is not a framebuffer device and "
			"VPLAYER_FB_WIDTH/VPLAYER_FB_HEIGHT are not set\n", path);
		return -1;
	}

	fb->width = atoi(w_env);
	fb->height = atoi(h_env);
	fb->bpp = 32;
	fb->stride = fb->width * 4;
	fb->mem_size = (size_t)fb->stride * fb->height;
	fb->is_mock = 1;

	fb->fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fb->fd < 0) {
		perror("fb: open mock");
		return -1;
	}
	if (ftruncate(fb->fd, (off_t)fb->mem_size) < 0) {
		perror("fb: ftruncate mock");
		close(fb->fd);
		return -1;
	}
	fb->mem = mmap(NULL, fb->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->mem == MAP_FAILED) {
		perror("fb: mmap mock");
		close(fb->fd);
		return -1;
	}
	return 0;
}

int fb_open(struct fb_dev *fb, const char *path)
{
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;

	fb->fd = open(path, O_RDWR);
	if (fb->fd < 0) {
		/* Not a real fb device, or doesn't exist yet (e.g. running on a
		 * desktop) -- fall back to a plain-file mock. */
		return open_mock(fb, path);
	}

	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
	    ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		close(fb->fd);
		return open_mock(fb, path);
	}

	fb->width = vinfo.xres;
	fb->height = vinfo.yres;
	fb->bpp = vinfo.bits_per_pixel;
	fb->stride = finfo.line_length;
	fb->mem_size = (size_t)finfo.smem_len;
	fb->is_mock = 0;

	fb->mem = mmap(NULL, fb->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->mem == MAP_FAILED) {
		perror("fb: mmap");
		close(fb->fd);
		return -1;
	}

	return 0;
}

void fb_wait_vsync(struct fb_dev *fb)
{
	if (fb->is_mock)
		return;
	int arg = 0;
	if (ioctl(fb->fd, FBIO_WAITFORVSYNC, &arg) < 0 && errno != ENOTTY)
		perror("fb: FBIO_WAITFORVSYNC");
}

void fb_blank(struct fb_dev *fb)
{
	memset(fb->mem, 0, fb->mem_size);
}

void fb_close(struct fb_dev *fb)
{
	if (fb->mem)
		munmap(fb->mem, fb->mem_size);
	if (fb->fd >= 0)
		close(fb->fd);
}
