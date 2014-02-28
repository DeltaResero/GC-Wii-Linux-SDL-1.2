/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2004 Sam Lantinga

    SDL port for the Nintendo GameCube
    Copyright (C) 2004-2006 Albert Herranz
    Accelerated yuv blitters courtesy of kirin.

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

    based on SDL_yuv_sw.c by

    Sam Lantinga
    slouken@libsdl.org
*/

/* This is the GameCube implementation of the YUV video overlay support */

/* This code was derived from code carrying the following copyright notices:

 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

 * Copyright (c) 1995 Erik Corry
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL ERIK CORRY BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
 * SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF
 * THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF ERIK CORRY HAS BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * ERIK CORRY SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
 * BASIS, AND ERIK CORRY HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT,
 * UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

 * Portions of this software Copyright (c) 1995 Brown University.
 * All rights reserved.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement
 * is hereby granted, provided that the above copyright notice and the
 * following two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL BROWN UNIVERSITY BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF BROWN
 * UNIVERSITY HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * BROWN UNIVERSITY SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
 * BASIS, AND BROWN UNIVERSITY HAS NO OBLIGATION TO PROVIDE MAINTENANCE,
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#define GC_DEBUG 1
//#undef GC_DEBUG

#include <stdlib.h>
#include <string.h>

#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_cpuinfo.h"
#include "../SDL_stretch_c.h"
#include "../SDL_yuvfuncs.h"
#include "SDL_gcyuv_c.h"
#include "SDL_gcvideo.h"

/* The functions used to manipulate software video overlays */
static struct private_yuvhwfuncs gc_yuvfuncs = {
	GC_LockYUV,
	GC_UnlockYUV,
	GC_DisplayYUV,
	GC_FreeYUV
};

/* RGB conversion lookup tables */
struct private_yuvhwdata {
	SDL_Surface *display;
	Uint8 *pixels;

	/* These are just so we don't have to allocate them separately */
	Uint16 pitches[3];
	Uint8 *planes[3];
};

/**
 *
 */
SDL_Overlay *GC_CreateYUVOverlay(_THIS, int width, int height, Uint32 format,
				 SDL_Surface * display)
{
	SDL_Overlay *overlay;
	struct private_yuvhwdata *swdata;

	/* Only RGB packed pixel conversion supported */
	if ((display->format->BytesPerPixel != 2) &&
	    (display->format->BytesPerPixel != 3) &&
	    (display->format->BytesPerPixel != 4)) {
		SDL_SetError("Can't use YUV data on non 16/24/32 bit surfaces");
		return (NULL);
	}

	/* Verify that we support the format */
	switch (format) {
	case SDL_YV12_OVERLAY:
	case SDL_IYUV_OVERLAY:
	case SDL_YUY2_OVERLAY:
	case SDL_UYVY_OVERLAY:
	case SDL_YVYU_OVERLAY:
		break;
	default:
		SDL_SetError("Unsupported YUV format");
		return (NULL);
	}

	/* Create the overlay structure */
	overlay = (SDL_Overlay *) malloc(sizeof *overlay);
	if (overlay == NULL) {
		SDL_OutOfMemory();
		return (NULL);
	}
	memset(overlay, 0, (sizeof *overlay));

	/* Fill in the basic members */
	overlay->format = format;
	overlay->w = width;
	overlay->h = height;

	/* Set up the YUV surface function structure */
	overlay->hwfuncs = &gc_yuvfuncs;

	/* Create the pixel data and lookup tables */
	swdata = (struct private_yuvhwdata *)malloc(sizeof *swdata);
	overlay->hwdata = swdata;
	if (swdata == NULL) {
		SDL_OutOfMemory();
		SDL_FreeYUVOverlay(overlay);
		return (NULL);
	}
	swdata->display = display;
	swdata->pixels = (Uint8 *) malloc(width * height * 2);

	if (!swdata->pixels) {
		SDL_OutOfMemory();
		SDL_FreeYUVOverlay(overlay);
		return NULL;
	}

	/* Find the pitch and offset values for the overlay */
	overlay->pitches = swdata->pitches;
	overlay->pixels = swdata->planes;
	switch (format) {
	case SDL_YV12_OVERLAY:
	case SDL_IYUV_OVERLAY:
		overlay->pitches[0] = overlay->w;
		overlay->pitches[1] = overlay->pitches[0] / 2;
		overlay->pitches[2] = overlay->pitches[0] / 2;
		overlay->pixels[0] = swdata->pixels;
		overlay->pixels[1] = overlay->pixels[0] +
		    overlay->pitches[0] * overlay->h;
		overlay->pixels[2] = overlay->pixels[1] +
		    overlay->pitches[1] * overlay->h / 2;
		overlay->planes = 3;
		break;
	case SDL_YUY2_OVERLAY:
	case SDL_UYVY_OVERLAY:
	case SDL_YVYU_OVERLAY:
		overlay->pitches[0] = overlay->w * 2;
		overlay->pixels[0] = swdata->pixels;
		overlay->planes = 1;
		break;
	default:
		/* We should never get here (caught above) */
		break;
	}

	/* We're all done.. */
	return (overlay);
}

/**
 *
 */
int GC_LockYUV(_THIS, SDL_Overlay * overlay)
{
	return (0);
}

/**
 *
 */
void GC_UnlockYUV(_THIS, SDL_Overlay * overlay)
{
	return;
}

static int not_warned = 1;

/**
 *
 */
int GC_DisplayYUV(_THIS, SDL_Overlay * overlay, SDL_Rect * dstrect)
{
	struct private_yuvhwdata *swdata;
	SDL_Surface *display;
	Uint8 *lum, *lum2, *Cr, *Cb;
	Uint8 *dst, *src;
	Uint32 *row;
	int mod;

	int i, height, width;
	int scale_YUV = 0;
	unsigned int src_cnt_x, dst_cnt_x;
	unsigned int src_cnt_y, dst_cnt_y;

	swdata = overlay->hwdata;
	display = swdata->display;

	switch (overlay->format) {
	case SDL_YV12_OVERLAY:
		if (not_warned)
			GC_DPRINTF("SDL_YV12_OVERLAY\n");
		lum = overlay->pixels[0];
		Cr = overlay->pixels[1];
		Cb = overlay->pixels[2];
		break;
	case SDL_IYUV_OVERLAY:
		if (not_warned)
			GC_DPRINTF("SDL_IYUV_OVERLAY\n");
		lum = overlay->pixels[0];
		Cr = overlay->pixels[2];
		Cb = overlay->pixels[1];
		break;
	case SDL_YUY2_OVERLAY:
		if (not_warned)
			GC_DPRINTF("SDL_YUY2_OVERLAY\n");
		lum = overlay->pixels[0];
		Cr = lum + 3;
		Cb = lum + 1;
		break;
	case SDL_UYVY_OVERLAY:
		if (not_warned)
			GC_DPRINTF("SDL_UYVY_OVERLAY\n");
		lum = overlay->pixels[0] + 1;
		Cr = lum + 1;
		Cb = lum - 1;
		break;
	case SDL_YVYU_OVERLAY:
		if (not_warned)
			GC_DPRINTF("SDL_YVYU_OVERLAY\n");
		lum = overlay->pixels[0];
		Cr = lum + 1;
		Cb = lum + 3;
		break;
	default:
		SDL_SetError("Unsupported YUV format in blit");
		return (-1);
	}

	if ((overlay->w != dstrect->w) || (overlay->h != dstrect->h)) {
		scale_YUV = 1;
		if (not_warned) {
			GC_DPRINTF("scaling %ix%i => %ix%i\n",
				   overlay->w, overlay->h, dstrect->w,
				   dstrect->h);
		}
	}

	/* must be a multiple of 2 */
	/* dstrect->w &= ~0x01; */

	if (SDL_MUSTLOCK(display)) {
		if (SDL_LockSurface(display) < 0) {
			return -1;
		}
	}

	GC_WaitForFlipCompletion(_this);

	dst = page_address[back_page] + page_offset +
	    (dstrect->y * display->pitch) + dstrect->x * 2;
	height = dstrect->h;	/* overlay->h; */
	width = dstrect->w * 2;	/* overlay->w * 2; */
	mod = display->pitch - width;

	if (!scale_YUV && overlay->format == SDL_YUY2_OVERLAY) {
		src = lum;
		while (height--) {
			memcpy(dst, src, width);
			src += width;
			dst += width + mod;
		}
	} else if (!scale_YUV
		   && (overlay->format == SDL_YV12_OVERLAY
		       || overlay->format == SDL_IYUV_OVERLAY)) {
		/* planar YUV without scaling */
		width >>= 2;
		mod >>= 2;

		row = (Uint32 *) dst;
		while (height--) {
			for (i = 0; i < width; i++) {
				*row++ =
				    lum[i << 1] << 24 | Cb[i] << 16 |
				    lum[(i << 1) + 1] << 8 | Cr[i];
			}
			lum += width << 1;
			if ((height & 0x01) == 0) {
				Cb += width;
				Cr += width;
			}
			row += mod;
		}
	} else if (overlay->format == SDL_YV12_OVERLAY
		   || overlay->format == SDL_IYUV_OVERLAY) {
		/* planar YUV with scaling */
		dst_cnt_x = 0;
		src_cnt_x = dstrect->w;
		dst_cnt_y = 0;
		src_cnt_y = dstrect->h;

		scale_YUV = 0;

		width >>= 2;
		mod >>= 2;

		row = (Uint32 *) dst;
		while (height--) {
			for (i = 0; i < width; i++) {
				dst_cnt_x += overlay->w;
				lum2 = lum;
				while (dst_cnt_x >= src_cnt_x) {
					src_cnt_x += dstrect->w;
					if ((unsigned int)lum2 & 0x01) {
						Cb++;
						Cr++;
					}
					lum2++;
				}

				*row++ =
				    *lum << 24 | *Cb << 16 | *lum2 << 8 | *Cr;

				dst_cnt_x += overlay->w;
				lum = lum2;
				while (dst_cnt_x >= src_cnt_x) {
					src_cnt_x += dstrect->w;
					if ((unsigned int)lum & 0x01) {
						Cb++;
						Cr++;
					}
					lum++;
				}
			}

			lum -= overlay->w;
			Cr -= overlay->w >> 1;
			Cb -= overlay->w >> 1;

			dst_cnt_y += overlay->h;
			while (dst_cnt_y >= src_cnt_y) {
				lum += overlay->w;

				if (scale_YUV == 1) {
					Cb += overlay->w >> 1;
					Cr += overlay->w >> 1;
					scale_YUV = 0;
				} else
					scale_YUV = 1;

				src_cnt_y += dstrect->h;
			}

			row += mod;
		}

	} else if (!scale_YUV) {
		/* packed YUV without scaling */
		width >>= 2;
		mod >>= 2;

		row = (Uint32 *) dst;
		while (height--) {
			for (i = 0; i < width; i++) {
				*row++ =
				    lum[0] << 24 | *Cb << 16 | lum[2] << 8 |
				    *Cr;
				lum += 4;
				Cb += 4;
				Cr += 4;
			}
			row += mod;
		}
	} else {
		/* packed YUV with scaling */
		dst_cnt_x = 0;
		src_cnt_x = dstrect->w;
		dst_cnt_y = 0;
		src_cnt_y = dstrect->h;

		width >>= 2;
		mod >>= 2;

		row = (Uint32 *) dst;
		while (height--) {
			for (i = 0; i < width; i++) {
				dst_cnt_x += overlay->w;
				lum2 = lum;
				while (dst_cnt_x >= src_cnt_x) {
					src_cnt_x += dstrect->w;
					if ((unsigned int)lum2 & 0x02) {
						Cb += 4;
						Cr += 4;
					}
					lum2 += 2;
				}

				*row++ =
				    *lum << 24 | *Cb << 16 | *lum2 << 8 | *Cr;

				dst_cnt_x += overlay->w;
				lum = lum2;
				while (dst_cnt_x >= src_cnt_x) {
					src_cnt_x += dstrect->w;
					if ((unsigned int)lum & 0x02) {
						Cb += 4;
						Cr += 4;
					}
					lum += 2;
				}
			}
			lum -= overlay->w << 1;
			Cr -= overlay->w << 1;
			Cb -= overlay->w << 1;

			dst_cnt_y += overlay->h;
			while (dst_cnt_y >= src_cnt_y) {
				lum += overlay->w << 1;
				Cb += overlay->w << 1;
				Cr += overlay->w << 1;
				src_cnt_y += dstrect->h;
			}

			row += mod;
		}
	}

	if (SDL_MUSTLOCK(display)) {
		SDL_UnlockSurface(display);
	}

	not_warned = 0;

	return (0);
}

/**
 *
 */
void GC_FreeYUV(_THIS, SDL_Overlay * overlay)
{
	struct private_yuvhwdata *swdata;

	swdata = overlay->hwdata;
	if (swdata) {
		if (swdata->pixels) {
			free(swdata->pixels);
		}
		free(swdata);
	}
}
