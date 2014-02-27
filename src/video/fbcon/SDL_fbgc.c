/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2009 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#ifdef SDL_VIDEO_DRIVER_GC
#include <errno.h>
#include "SDL_video.h"
#include "../SDL_blit.h"
#include "SDL_fbgc.h"

static Uint32 r_Yr[256];
static Uint32 g_Yg_[256];
static Uint32 b_Yb[256];
static Uint32 r_Ur[256];
static Uint32 g_Ug_[256];
static Uint32 b_Ub[256];
/* static Uint32 r_Vr[256]; // space and cache optimisation */
#define r_Vr b_Ub
static Uint32 g_Vg_[256];
static Uint32 b_Vb[256];

static Uint8 RGB16toY[1 << 16];
static Uint8 RGB16toU[1 << 16];
static Uint8 RGB16toV[1 << 16];

#ifndef FBIOFLIPHACK
#define FBIOFLIPHACK	0x4623 /* gc-linux */
#endif

#ifndef GC_BLACK
#define GC_BLACK 0x00800080
#endif

#ifdef GC_DEBUG
#  define GC_DPRINTF(fmt, args...) \
	  fprintf(stderr,"DDD|%s: " fmt, __FUNCTION__ , ## args)
#else
#  define GC_DPRINTF(fmt, args...)
#endif

SDL_bool GC_Test(_THIS)
{
	int fliptest;
	if (ioctl(console_fd, FBIOFLIPHACK, &fliptest))
		return SDL_TRUE;
	return SDL_FALSE;
}

SDL_bool GC_Available(void)
{
	if (access("/sys/bus/platform/drivers/gcn-vifb", 0) == 0)
		return SDL_TRUE; /* (For kernels 2.6.36 and newer) */
	else if (access("/sys/bus/of_platform/drivers/gcn-vifb", 0) == 0)
		return SDL_TRUE; /* (For kernels prior to 2.6.36) */
	else
		return SDL_FALSE;
}

/*
 *
 * Color space handling.
 */

#define RGB2YUV_SHIFT   16
#define RGB2YUV_LUMA    16
#define RGB2YUV_CHROMA 128

#define Yr ((int)( 0.299*(1<<RGB2YUV_SHIFT)))
#define Yg ((int)( 0.587*(1<<RGB2YUV_SHIFT)))
#define Yb ((int)( 0.114*(1<<RGB2YUV_SHIFT)))

#define Ur ((int)(-0.169*(1<<RGB2YUV_SHIFT)))
#define Ug ((int)(-0.331*(1<<RGB2YUV_SHIFT)))
#define Ub ((int)( 0.500*(1<<RGB2YUV_SHIFT)))

#define Vr ((int)( 0.500*(1<<RGB2YUV_SHIFT)))	/* same as Ub */
#define Vg ((int)(-0.419*(1<<RGB2YUV_SHIFT)))
#define Vb ((int)(-0.081*(1<<RGB2YUV_SHIFT)))

#define clamp(x, y, z) ((z < x) ? x : ((z > y) ? y : z))

static void GC_InitRGB2YUVTables(void)
{
	unsigned int i;
	unsigned int r, g, b;

	for (i = 0; i < 256; i++) {
		r_Yr[i] = Yr * i;
		g_Yg_[i] = Yg * i + (RGB2YUV_LUMA << RGB2YUV_SHIFT);
		b_Yb[i] = Yb * i;
		r_Ur[i] = Ur * i;
		g_Ug_[i] = Ug * i + (RGB2YUV_CHROMA << RGB2YUV_SHIFT);
		b_Ub[i] = Ub * i;
		r_Vr[i] = Vr * i;
		g_Vg_[i] = Vg * i + (RGB2YUV_CHROMA << RGB2YUV_SHIFT);
		b_Vb[i] = Vb * i;
	}

	for (i = 0; i < 1 << 16; i++) {
		/* RGB565 */
		r = ((i >> 8) & 0xf8);
		g = ((i >> 3) & 0xfc);
		b = ((i << 3) & 0xf8);
		/* extend to 8bit */
		r |= (r >> 5);
		g |= (g >> 6);
		b |= (b >> 5);

		RGB16toY[i] =
		    clamp(16, 235,
			  (r_Yr[r] + g_Yg_[g] + b_Yb[b]) >> RGB2YUV_SHIFT);
		RGB16toU[i] =
		    clamp(16, 240,
			  (r_Ur[r] + g_Ug_[g] + b_Ub[b]) >> RGB2YUV_SHIFT);
		RGB16toV[i] =
		    clamp(16, 240,
			  (r_Vr[r] + g_Vg_[g] + b_Vb[b]) >> RGB2YUV_SHIFT);
	}
}

static inline Uint32 rgbrgb16toyuy2(Uint16 rgb1, Uint16 rgb2)
{
	register int Y1, Cb, Y2, Cr;
	Uint16 rgb;

	/* fast path, thanks to bohdy */
	if (!(rgb1 | rgb2)) {
		return GC_BLACK;	/* black, black */
	}

	if (rgb1 == rgb2) {
		/* fast path, thanks to isobel */
		Y1 = Y2 = RGB16toY[rgb1];
		Cb = RGB16toU[rgb1];
		Cr = RGB16toV[rgb1];
	} else {
		Y1 = RGB16toY[rgb1];
		Y2 = RGB16toY[rgb2];

		/* RGB565 average */
		rgb = ((rgb1 >> 1) & 0xFBEF) + ((rgb2 >> 1) & 0xFBEF) +
		    ((rgb1 & rgb2) & 0x0821);

		Cb = RGB16toU[rgb];
		Cr = RGB16toV[rgb];
	}

	return (((char)Y1) << 24) | (((char)Cb) << 16) | (((char)Y2) << 8)
	    | (((char)Cr) << 0);
}

/*
 *
 * Blitters.
 */
static void GC_UpdateRectRGB16(_THIS, SDL_Rect * rect, int pitch)
{
	int width, height, left, i, mod, mod32;
	Uint8 *src, *dst;
	Uint32 *src32, *dst32;
	Uint16 *rgb;

	/* XXX case width < 2 needs special treatment */

	/* in pixel units */
	left = rect->x & ~1;	/* 2 pixel align */
	width = (rect->w + 1) & ~1;	/* 2 pixel align in excess */
	height = rect->h;

	/* in bytes, src and dest are 16bpp */
	src = shadow_mem + (rect->y * pitch) + left * 2;
	dst = flip_address[back_page] + page_offset +
		 (rect->y * pitch) + left * 2;
	mod = pitch - width * 2;

	src32 = (Uint32 *) src;
	dst32 = (Uint32 *) dst;
	mod32 = mod / 4;

	while (height--) {
		i = width / 2;

		while (i--) {
			rgb = (Uint16 *) src32;
			*dst32++ = rgbrgb16toyuy2(rgb[0], rgb[1]);
			src32++;
		}
		src32 += mod32;
		dst32 += mod32;
	}
}

void GC_Init(_THIS, SDL_PixelFormat *vformat)
{
	GC_InitRGB2YUVTables();

	/* 16 bits per pixel */
	vformat->BitsPerPixel = 16;
	vformat->BytesPerPixel = 2;
	/* RGB565 */
	vformat->Rmask = 0x0000f800;
	vformat->Gmask = 0x000007e0;
	vformat->Bmask = 0x0000001f;

	shadow_fb = 1;
}

/*
 *
 * Video mode handling.
 */

/* only 640x480 16bpp is currently supported */
const static SDL_Rect RECT_640x480 = { 0, 0, 640, 480 };
const static SDL_Rect *vid_modes[] = {
	&RECT_640x480,
	NULL
};

static SDL_Rect **GC_ListModes(_THIS, SDL_PixelFormat * format, Uint32 flags)
{
	switch (format->BitsPerPixel) {
	case 16:
		return (SDL_Rect **) vid_modes;
	default:
		return NULL;
	}
}

SDL_Surface *GC_SetVideoMode(_THIS, SDL_Surface * current,
			     int width, int height, int bpp, Uint32 flags)
{
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;
	int i;
	Uint32 Rmask;
	Uint32 Gmask;
	Uint32 Bmask;
	Uint32 *p, *q;
	Uint32 yres;

	GC_DPRINTF("Setting %dx%d %dbpp %smode\n", width, height, bpp,
			(flags & SDL_DOUBLEBUF)?"(doublebuf) ":"");

	/* Set the terminal into graphics mode */
	if (FB_EnterGraphicsMode(this) < 0) {
		return (NULL);
	}

	/* Restore the original palette */
	//FB_RestorePalette(this);

	/* Set the video mode and get the final screen format */
	if (ioctl(console_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		SDL_SetError("Couldn't get console screen info");
		return (NULL);
	}

	yres = vinfo.yres;

	/* hack to center 640x480 resolution on PAL cubes */
	if (vinfo.xres == 640 && vinfo.yres == 576) {
		page_offset = ((576 - 480) / 2) * 640 * ((bpp + 7) / 8);
	} else {
		page_offset = 0;
	}

	/* clear all video memory */
	p = (Uint32 *)mapped_mem;
	q = (Uint32 *)(mapped_mem + mapped_memlen);
	while (p < q)
		*p++ = GC_BLACK;

	if ((vinfo.xres != width) || (vinfo.yres != height) ||
	    (vinfo.bits_per_pixel != bpp)) {
		vinfo.activate = FB_ACTIVATE_NOW;
		vinfo.accel_flags = 0;
		vinfo.bits_per_pixel = bpp;
		vinfo.xres = width;
		vinfo.xres_virtual = width;
		/* do not modify yres*, we use a fake 640x480 mode in PAL */
		//vinfo.yres = height;
		//vinfo.yres_virtual = 2*height;
		vinfo.xoffset = 0;
		vinfo.yoffset = 0;
		vinfo.red.length = vinfo.red.offset = 0;
		vinfo.green.length = vinfo.green.offset = 0;
		vinfo.blue.length = vinfo.blue.offset = 0;
		vinfo.transp.length = vinfo.transp.offset = 0;

		if (ioctl(console_fd, FBIOPUT_VSCREENINFO, &vinfo) < 0) {
			SDL_SetError("Couldn't set console screen info");
			return (NULL);
		}
	} else {
		int maxheight;

		/* Figure out how much video memory is available */
		maxheight = 2*yres;
		if (vinfo.yres_virtual > maxheight) {
			vinfo.yres_virtual = maxheight;
		}
	}
	cache_vinfo = vinfo;

	Rmask = 0;
	for (i = 0; i < vinfo.red.length; ++i) {
		Rmask <<= 1;
		Rmask |= (0x00000001 << vinfo.red.offset);
	}
	Gmask = 0;
	for (i = 0; i < vinfo.green.length; ++i) {
		Gmask <<= 1;
		Gmask |= (0x00000001 << vinfo.green.offset);
	}
	Bmask = 0;
	for (i = 0; i < vinfo.blue.length; ++i) {
		Bmask <<= 1;
		Bmask |= (0x00000001 << vinfo.blue.offset);
	}
	if (!SDL_ReallocFormat(current, bpp, Rmask, Gmask, Bmask, 0)) {
		return (NULL);
	}

	/* Get the fixed information about the console hardware.
	   This is necessary since finfo.line_length changes.
	 */
	if (ioctl(console_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		SDL_SetError("Couldn't get console hardware info");
		return (NULL);
	}

	/* Save hardware palette, if needed */
	//FB_SavePalette(this, &finfo, &vinfo);

	/* Set up the new mode framebuffer */
	current->flags = SDL_FULLSCREEN;
	current->w = width;
	current->h = height;
	current->pitch = width * ((bpp + 7) / 8);
	current->pixels = shadow_mem;

	flip_address[0] = mapped_mem;
	flip_address[1] = mapped_mem + current->pitch * yres;

	back_page = 1;
	if (flags & SDL_DOUBLEBUF) {
		current->flags |= SDL_DOUBLEBUF;
		flip_pending = 1;
	} else {
		flip_pending = 0;
		/* make page 0 both the visible and back page */
		back_page = ioctl(console_fd, FBIOFLIPHACK, &back_page);
		if (back_page < 0)
			back_page = 0;
	}

	/* Set the update rectangle function */
	switch (bpp) {
	case 16:
		GC_DPRINTF("Using 16bpp blitter\n");
		this->hidden->UpdateRect = GC_UpdateRectRGB16;
		break;
	default:
		GC_DPRINTF("Using NO blitter\n");
		this->hidden->UpdateRect = NULL;
		break;
	}

	/* Handle OpenGL support */
#ifdef HAVE_OPENGL
	if (flags & SDL_OPENGL) {
		if (GC_GL_CreateWindow(this, width, height) == 0) {
			current->flags |= (SDL_OPENGL | SDL_FULLSCREEN);
		} else {
			current = NULL;
		}
	}
#endif				/* HAVE_OPENGL */

	/* We're done */
	return (current);
}

static int GC_FlipHWSurface(_THIS, SDL_Surface * surface)
{
	if (flip_pending) {
		/* SDL_UpdateRect was not called */
		SDL_UpdateRect(this->screen, 0, 0, 0, 0);
	}

	/* flip video page as soon as possible */
	ioctl(console_fd, FBIOFLIPHACK, NULL);
	flip_pending = 1;

	return (0);
}

static void GC_WaitForFlipCompletion(_THIS)
{
	int visible_page;
	int result;

	if (flip_pending) {
		flip_pending = 0;
		back_page ^= 1;
		visible_page = back_page;
		while (visible_page == back_page) {
			/* wait until back_page is not visible */
			result = ioctl(console_fd, FBIOFLIPHACK, &back_page);
			if (result < 0) {
				if ((errno == EINTR) || (errno == EAGAIN))
					continue;
				return; /* ioctl unsupported ... */
			}
			visible_page = result;
		}
		/*
		 * At this point the back_page is not visible. We can safely
		 * write to it without tearing.
		 */
	}
}

static void GC_UpdateRects(_THIS, int numrects, SDL_Rect * rects)
{
	SDL_Surface *screen;
	int pitch;

	/* external yuy2 fb is 16bpp */

	screen = this->screen;
	pitch = screen->pitch;	/* this is the pitch of the shadow buffer */

	if (this->hidden->UpdateRect) {
		GC_WaitForFlipCompletion(this);
		while (numrects--) {
			if (rects->w <= 0 || rects->h <= 0)
				continue;
			this->hidden->UpdateRect(this, rects, pitch);
			rects++;
		}
	}
}

void GC_CreateDevice(SDL_VideoDevice *this)
{
	this->ListModes = GC_ListModes;
	this->SetVideoMode = GC_SetVideoMode;
	this->FlipHWSurface = GC_FlipHWSurface;
	this->UpdateRects = GC_UpdateRects;
}

#endif
