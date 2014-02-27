/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2004 Sam Lantinga

    SDL port for the Nintendo GameCube
    Copyright (C) 2004-2005 Albert Herranz

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

    Sam Lantinga
    slouken@libsdl.org
*/

#ifndef _SDL_gcvideo_h
#define _SDL_gcvideo_h

#include <sys/types.h>
#include <termios.h>
#include <linux/fb.h>

#ifdef GC_DEBUG
#  define GC_DPRINTF(fmt, args...) \
	  fprintf(stderr,"DDD|%s: " fmt, __FUNCTION__ , ## args)
#else
#  define GC_DPRINTF(fmt, args...)
#endif

#include "SDL_sysvideo.h"
#include "SDL_mutex.h"

#define FBIOFLIPHACK            0x4623 /* gc-linux */


/* Hidden "this" pointer for the video functions */
#define _THIS	SDL_VideoDevice *_this

extern void GC_WaitForFlipCompletion(_THIS);

/* Private display data */

struct SDL_PrivateVideoData {
	int w, h;
	void *buffer;

	void (*UpdateRect) (_THIS, SDL_Rect * rect, int pitch);

	int console_fd;
	struct fb_var_screeninfo cache_vinfo;
	struct fb_var_screeninfo saved_vinfo;

	int current_vt;
	int saved_vt;
	int keyboard_fd;
	int in_graphics_mode;

	//int mouse_fd;

	char *mapped_mem;
	int mapped_memlen;

	int flip_pending;
	int back_page;
	char *page_address[2];
	int page_offset;

#define NUM_MODELISTS   4	/* 8, 16, 24, and 32 bits-per-pixel */
	int SDL_nummodes[NUM_MODELISTS];
	SDL_Rect **SDL_modelist[NUM_MODELISTS];

//	SDL_mutex *hw_lock;
};

/* Old variable names */
#define console_fd              (_this->hidden->console_fd)
#define cache_vinfo             (_this->hidden->cache_vinfo)
#define saved_vinfo             (_this->hidden->saved_vinfo)
#define current_vt              (_this->hidden->current_vt)
#define saved_vt                (_this->hidden->saved_vt)
#define keyboard_fd             (_this->hidden->keyboard_fd)
#define in_graphics_mode	(_this->hidden->in_graphics_mode)
//#define mouse_fd                (_this->hidden->mouse_fd)
#define mapped_mem              (_this->hidden->mapped_mem)
#define mapped_memlen           (_this->hidden->mapped_memlen)
#define flip_pending            (_this->hidden->flip_pending)
#define back_page               (_this->hidden->back_page)
#define page_address            (_this->hidden->page_address)
#define page_offset             (_this->hidden->page_offset)
#define SDL_nummodes            (_this->hidden->SDL_nummodes)
#define SDL_modelist            (_this->hidden->SDL_modelist)
//#define hw_lock                 (_this->hidden->hw_lock)

#endif				/* _SDL_gcvideo_h */
