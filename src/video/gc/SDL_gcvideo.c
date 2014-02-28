/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2004 Sam Lantinga

    SDL port for the Nintendo GameCube
    Copyright (C) 2004-2006 Albert Herranz

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    based on SDL_nullvideo.c by

    Sam Lantinga
    slouken@libsdl.org
*/

#define GC_DEBUG 1
//#undef GC_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#ifndef HAVE_GETPAGESIZE
#include <asm/page.h>		/* For definition of PAGE_SIZE */
#endif

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_gcvideo.h"
#include "SDL_gcyuv_c.h"
#include "SDL_gcevents_c.h"

#ifdef HAVE_OPENGL
#include "SDL_gcgl.h"
#endif /* HAVE_OPENGL */

/* Initialization/Query functions */
static int GC_VideoInit(_THIS, SDL_PixelFormat * vformat);
static SDL_Rect **GC_ListModes(_THIS, SDL_PixelFormat * format, Uint32 flags);
static SDL_Surface *GC_SetVideoMode(_THIS, SDL_Surface * current, int width,
				    int height, int bpp, Uint32 flags);
static int GC_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color * colors);
static void GC_VideoQuit(_THIS);

/* Hardware surface functions */
static int GC_AllocHWSurface(_THIS, SDL_Surface * surface);
static int GC_LockHWSurface(_THIS, SDL_Surface * surface);
static void GC_UnlockHWSurface(_THIS, SDL_Surface * surface);
static void GC_FreeHWSurface(_THIS, SDL_Surface * surface);
static int GC_FlipHWSurface(_THIS, SDL_Surface * surface);

/* etc. */
static void GC_InitRGB2YUVTables(void);
static void GC_UpdateRects(_THIS, int numrects, SDL_Rect * rects);
static void GC_UpdateRectRGB16(_THIS, SDL_Rect * rect, int pitch);
void GC_WaitForFlipCompletion(_THIS);

#define GC_BLACK 0x00800080


/*
 *
 * Bootstrap functions.
 */

/*
 *
 */
static int GC_Available(void)
{
	int console;
	const char *SDL_fbdev;

	/*
	 * XXX We could check that
	 * "/sys/devices/platform/gcnfb.0/graphics\:fb0/name"
	 * exists and contains gcnfb.
	 */

	SDL_fbdev = getenv("SDL_FBDEV");
	if (SDL_fbdev == NULL) {
		SDL_fbdev = "/dev/fb0";
	}
	console = open(SDL_fbdev, O_RDWR, 0);
	if (console >= 0) {
		close(console);
	}
	return (console >= 0);
}

/*
 *
 */
static void GC_DeleteDevice(_THIS)
{
	if (_this->hidden->buffer) {
		free(_this->hidden->buffer);
		_this->hidden->buffer = NULL;
	}
	free(_this->hidden);
#ifdef HAVE_OPENGL
	if (_this->gl_data)
		free(_this->gl_data);
#endif				/* HAVE_OPENGL */
	free(_this);
}

/*
 *
 */
static SDL_VideoDevice *GC_CreateDevice(int devindex)
{
	_THIS;

	/* Initialize all variables that we clean on shutdown */
	_this = (SDL_VideoDevice *) malloc(sizeof(SDL_VideoDevice));
	if (_this) {
		memset(_this, 0, (sizeof *_this));

#ifdef HAVE_OPENGL
		_this->gl_data = (struct SDL_PrivateGLData *)
		    malloc((sizeof *_this->gl_data));
		if (_this->gl_data == NULL) {
			SDL_OutOfMemory();
			free(_this);
			return 0;
		}
		memset(_this->gl_data, 0, (sizeof *_this->gl_data));
#endif /* HAVE_OPENGL */

		_this->hidden = (struct SDL_PrivateVideoData *)
		    malloc((sizeof *_this->hidden));
		if (_this->hidden == NULL) {
			SDL_OutOfMemory();
#ifdef HAVE_OPENGL
			free(_this->gl_data);
#endif /* HAVE_OPENGL */
			free(_this);
			return (0);
		}
	}
	memset(_this->hidden, 0, (sizeof *_this->hidden));

	keyboard_fd = -1;
	in_graphics_mode = 0;

	/* Set the function pointers */
	_this->VideoInit = GC_VideoInit;
	_this->ListModes = GC_ListModes;
	_this->SetVideoMode = GC_SetVideoMode;
	_this->CreateYUVOverlay = GC_CreateYUVOverlay;
	_this->SetColors = GC_SetColors;
	_this->UpdateRects = GC_UpdateRects;
	_this->VideoQuit = GC_VideoQuit;
	_this->AllocHWSurface = GC_AllocHWSurface;
	_this->CheckHWBlit = NULL;
	_this->FillHWRect = NULL;
	_this->SetHWColorKey = NULL;
	_this->SetHWAlpha = NULL;
	_this->LockHWSurface = GC_LockHWSurface;
	_this->UnlockHWSurface = GC_UnlockHWSurface;
	_this->FlipHWSurface = GC_FlipHWSurface;
	_this->FreeHWSurface = GC_FreeHWSurface;
#ifdef HAVE_OPENGL
	_this->GL_LoadLibrary = GC_GL_LoadLibrary;
	_this->GL_GetProcAddress = GC_GL_GetProcAddress;
	_this->GL_GetAttribute = GC_GL_GetAttribute;
	_this->GL_MakeCurrent = GC_GL_MakeCurrent;
	_this->GL_SwapBuffers = GC_GL_SwapBuffers;
#endif				/* HAVE_OPENGL */
	_this->SetCaption = NULL;
	_this->SetIcon = NULL;
	_this->IconifyWindow = NULL;
	_this->GrabInput = NULL;
	_this->GetWMInfo = NULL;
	_this->InitOSKeymap = GC_InitOSKeymap;
	_this->PumpEvents = GC_PumpEvents;

	_this->free = GC_DeleteDevice;

	return _this;
}

VideoBootStrap GC_bootstrap = {
	"gcvideo", "GameCube Linux Video",
	GC_Available, GC_CreateDevice
};


/*
 *
 * Video setup routines.
 */

static int SDL_getpagesize(void)
{
#ifdef HAVE_GETPAGESIZE
	return getpagesize();
#elif defined(PAGE_SIZE)
	return PAGE_SIZE;
#else
#error Can not determine system page size.
	return 4096;  /* on Linux PPC32 page size is 4K */
#endif
}

/*
 *
 */
int GC_VideoInit(_THIS, SDL_PixelFormat * vformat)
{
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;
	const char *fbdev;
	int offset;

	GC_InitRGB2YUVTables();

	/* Initialize the library */
	fbdev = getenv("SDL_FBDEV");
	if (fbdev == NULL) {
		fbdev = "/dev/fb0";
	}
	console_fd = open(fbdev, O_RDWR, 0);
	if (console_fd < 0) {
		SDL_SetError("Unable to open %s", fbdev);
		return (-1);
	}

	/* Get the type of video hardware */
	if (ioctl(console_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
		SDL_SetError("Couldn't get console hardware info");
		GC_VideoQuit(_this);
		return (-1);
	}
	switch (finfo.type) {
	case FB_TYPE_PACKED_PIXELS:
		/* Supported, no worries.. */
		break;
	default:
		SDL_SetError("Unsupported console hardware");
		GC_VideoQuit(_this);
		return (-1);
	}
	switch (finfo.visual) {
	case FB_VISUAL_TRUECOLOR:
		/* Supported, no worries.. */
		break;
	default:
		SDL_SetError("Unsupported console hardware");
		GC_VideoQuit(_this);
		return (-1);
	}

	/* Memory map the device, compensating for buggy PPC mmap() */
	offset = (((long)finfo.smem_start) -
		  (((long)finfo.smem_start) & ~(SDL_getpagesize() - 1)));
	mapped_memlen = finfo.smem_len + offset;
	mapped_mem = mmap(NULL, mapped_memlen,
			  PROT_READ | PROT_WRITE, MAP_SHARED, console_fd, 0);
	if (mapped_mem == (char *)-1) {
		SDL_SetError("Unable to memory map the video hardware");
		mapped_mem = NULL;
		GC_VideoQuit(_this);
		return (-1);
	}

	/* Determine the current screen depth */
	if (ioctl(console_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
		SDL_SetError("Couldn't get console pixel format");
		GC_VideoQuit(_this);
		return (-1);
	}

	/* XXX isobel, review */

	/* 16 bits per pixel */
	vformat->BitsPerPixel = 16;
	vformat->BytesPerPixel = 2;
	/* RGB565 */
	vformat->Rmask = 0x0000f800;
	vformat->Gmask = 0x000007e0;
	vformat->Bmask = 0x0000001f;

	saved_vinfo = vinfo;

	vinfo.accel_flags = 0;
	ioctl(console_fd, FBIOPUT_VSCREENINFO, &vinfo);

	/* Fill in our hardware acceleration capabilities */
	_this->info.wm_available = 0;
	_this->info.hw_available = 1;
	_this->info.video_mem = finfo.smem_len / 1024;

	/* let's say "enable" keyboard support */
	if (GC_OpenKeyboard(_this) < 0) {
		GC_VideoQuit(_this);
		return (-1);
	}

	/* We're done! */
	return (0);
}

/*
 *
 * Note: If we are terminated, this could be called in the middle of
 *       another SDL video routine -- notably UpdateRects.
 */
static void GC_VideoQuit(_THIS)
{
	if (_this->hidden->buffer) {
		free(_this->hidden->buffer);
		_this->hidden->buffer = NULL;
	}
	if (_this->screen) {
		Uint32 *rowp = (Uint32 *) (mapped_mem + page_offset);
		int left =
		    (_this->screen->pitch * _this->screen->h) / sizeof(Uint32);
		while (left--) {
			*rowp++ = GC_BLACK;
		}
		if (_this->screen->pixels != NULL) {
			/* already freed above */
			_this->screen->pixels = NULL;
		}
	}
	/* Close console and input file descriptors */
	if (console_fd > 0) {
		/* Unmap the video framebuffer and I/O registers */
		if (mapped_mem) {
			munmap(mapped_mem, mapped_memlen);
			mapped_mem = NULL;
		}

		/* Restore the original video mode and palette */
		if (GC_InGraphicsMode(_this)) {
			//FB_RestorePalette(_this);
			ioctl(console_fd, FBIOPUT_VSCREENINFO, &saved_vinfo);
		}

		/* We're all done with the framebuffer */
		close(console_fd);
		console_fd = -1;
	}
	GC_CloseKeyboard(_this);
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

/*
 *
 */
SDL_Rect **GC_ListModes(_THIS, SDL_PixelFormat * format, Uint32 flags)
{
	switch (format->BitsPerPixel) {
	case 32:
		GC_DPRINTF("Hey! Hey! Someone asking for a 32bpp mode!\n");
		return (SDL_Rect **) & vid_modes[0];
	case 24:
		GC_DPRINTF("Hey! Hey! Someone asking for a 24bpp mode!\n");
		return (SDL_Rect **) & vid_modes[0];
	case 16:
		return (SDL_Rect **) & vid_modes[0];
	default:
		return NULL;
	}
}

/*
 *
 */
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
	if (GC_EnterGraphicsMode(_this) < 0) {
		return (NULL);
	}

	/* Restore the original palette */
	//FB_RestorePalette(_this);

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
	//FB_SavePalette(_this, &finfo, &vinfo);

	if (_this->hidden->buffer) {
		free(_this->hidden->buffer);
		_this->hidden->buffer = NULL;
	}

	_this->hidden->buffer = malloc(width * height * ((bpp + 7) / 8));
	if (!_this->hidden->buffer) {
		SDL_SetError("Couldn't allocate buffer for requested mode");
		return (NULL);
	}

	/* Set up the new mode framebuffer */
	current->flags = SDL_FULLSCREEN;
	current->w = width;
	current->h = height;
	current->pitch = width * ((bpp + 7) / 8);
	current->pixels = _this->hidden->buffer;

	page_address[0] = mapped_mem;
	page_address[1] = mapped_mem + current->pitch * yres;

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
		_this->hidden->UpdateRect = GC_UpdateRectRGB16;
		break;
	default:
		GC_DPRINTF("Using NO blitter\n");
		_this->hidden->UpdateRect = NULL;
		break;
	}

	/* Handle OpenGL support */
#ifdef HAVE_OPENGL
	if (flags & SDL_OPENGL) {
		if (GC_GL_CreateWindow(_this, width, height) == 0) {
			current->flags |= (SDL_OPENGL | SDL_FULLSCREEN);
		} else {
			current = NULL;
		}
	}
#endif				/* HAVE_OPENGL */

	/* We're done */
	return (current);
}


/*
 *
 * Hardware surface hooks.
 */

/*
 *
 */
static int GC_AllocHWSurface(_THIS, SDL_Surface * surface)
{
	/* We don't actually allow hardware surfaces other than the main one */
	return (-1);
}

/*
 *
 */
static void GC_FreeHWSurface(_THIS, SDL_Surface * surface)
{
	return;
}

/*
 *
 */
static int GC_LockHWSurface(_THIS, SDL_Surface * surface)
{
	return (0);
}

/*
 *
 */
static void GC_UnlockHWSurface(_THIS, SDL_Surface * surface)
{
	return;
}

/*
 *
 */
static int GC_FlipHWSurface(_THIS, SDL_Surface * surface)
{
	if (flip_pending) {
		/* SDL_UpdateRect was not called */
		SDL_UpdateRect(_this->screen, 0, 0, 0, 0);
	}

	/* flip video page as soon as possible */
	ioctl(console_fd, FBIOFLIPHACK, NULL);
	flip_pending = 1;

	return (0);
}

/*
 *
 */
void GC_WaitForFlipCompletion(_THIS)
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

/*
 *
 */
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

/*
 *
 */
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
 */
static int GC_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color * colors)
{
	int i;
	__u16 r[256];
	__u16 g[256];
	__u16 b[256];
	struct fb_cmap cmap;

	/* Set up the colormap */
	for (i = 0; i < ncolors; i++) {
		r[i] = colors[i].r << 8;
		g[i] = colors[i].g << 8;
		b[i] = colors[i].b << 8;
	}
	cmap.start = firstcolor;
	cmap.len = ncolors;
	cmap.red = r;
	cmap.green = g;
	cmap.blue = b;
	cmap.transp = NULL;

	if ((ioctl(console_fd, FBIOPUTCMAP, &cmap) < 0) ||
	    !(_this->screen->flags & SDL_HWPALETTE)) {
		colors = _this->screen->format->palette->colors;
		ncolors = _this->screen->format->palette->ncolors;
		cmap.start = 0;
		cmap.len = ncolors;
		memset(r, 0, sizeof(r));
		memset(g, 0, sizeof(g));
		memset(b, 0, sizeof(b));
		if (ioctl(console_fd, FBIOGETCMAP, &cmap) == 0) {
			for (i = ncolors - 1; i >= 0; --i) {
				colors[i].r = (r[i] >> 8);
				colors[i].g = (g[i] >> 8);
				colors[i].b = (b[i] >> 8);
			}
		}
		return (0);
	}
	return (1);
}


/*
 *
 * Blitters.
 */

/*
 *
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
	src = _this->hidden->buffer + (rect->y * pitch) + left * 2;
	dst = page_address[back_page] + page_offset +
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

/*
 *
 */
static void GC_UpdateRects(_THIS, int numrects, SDL_Rect * rects)
{
	SDL_Surface *screen;
	int pitch;

	/* external yuy2 fb is 16bpp */

	screen = _this->screen;
	pitch = screen->pitch;	/* this is the pitch of the shadow buffer */

	if (_this->hidden->UpdateRect) {
		GC_WaitForFlipCompletion(_this);
		while (numrects--) {
			if (rects->w <= 0 || rects->h <= 0)
				continue;
			_this->hidden->UpdateRect(_this, rects, pitch);
			rects++;
		}
	}
}

