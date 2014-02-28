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

    based on SDL_yuv_sw_c.h by

    Sam Lantinga
    slouken@libsdl.org
*/

#include "SDL_video.h"
#include "../SDL_sysvideo.h"

/* This is the GameCube implementation of the YUV video overlay support */

extern SDL_Overlay *GC_CreateYUVOverlay(_THIS, int width, int height,
					Uint32 format, SDL_Surface * display);

extern int GC_LockYUV(_THIS, SDL_Overlay * overlay);

extern void GC_UnlockYUV(_THIS, SDL_Overlay * overlay);

extern int GC_DisplayYUV(_THIS, SDL_Overlay * overlay, SDL_Rect * dstrect);

extern void GC_FreeYUV(_THIS, SDL_Overlay * overlay);
