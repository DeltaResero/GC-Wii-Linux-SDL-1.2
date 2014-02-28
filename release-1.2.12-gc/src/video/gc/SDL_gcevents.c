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

    based on SDL_fbevents.c by

    Sam Lantinga
    slouken@libsdl.org
*/

//#define DEBUG_KEYBOARD 1

/* Handle the event stream, converting console events into SDL events */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* For parsing /proc */
#include <dirent.h>
#include <ctype.h>

#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/keyboard.h>

#include "SDL.h"
#include "SDL_mutex.h"
#include "../../events/SDL_sysevents.h"
#include "../SDL_sysvideo.h"
#include "../../events/SDL_events_c.h"
#include "SDL_gcvideo.h"
#include "SDL_gcevents_c.h"

/**
 *
 */
int GC_InGraphicsMode(_THIS)
{
	return in_graphics_mode;
}

/**
 *
 */
int GC_EnterGraphicsMode(_THIS)
{
	char *already_in_graphics_mode;

	/* do nothing if we are told so */
	already_in_graphics_mode = getenv("SDL_HACK_GCAIGM");
	if (already_in_graphics_mode) {
		in_graphics_mode = 1;
		return keyboard_fd;
	}

	if (!GC_InGraphicsMode(_this)) {
		/* Switch to the correct virtual terminal */
		if (current_vt > 0) {
			struct vt_stat vtstate;

			if (ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0) {
				saved_vt = vtstate.v_active;
			}
			if (ioctl(keyboard_fd, VT_ACTIVATE, current_vt) == 0) {
				ioctl(keyboard_fd, VT_WAITACTIVE, current_vt);
			}
		}

		/* switch to graphics mode */
		if (keyboard_fd > 0) {
			ioctl(keyboard_fd, KDSETMODE, KD_GRAPHICS);
		}
		in_graphics_mode = 1;
	}
	return (keyboard_fd);
}

/**
 *
 */
void GC_LeaveGraphicsMode(_THIS)
{
	char *already_in_graphics_mode;

	/* do nothing if we are told so */
	already_in_graphics_mode = getenv("SDL_HACK_GCAIGM");
	if (already_in_graphics_mode) {
		in_graphics_mode = 0;
		return;
	}

	if (GC_InGraphicsMode(_this)) {
		/* switch to text mode  */
		if (keyboard_fd > 0) {
			ioctl(keyboard_fd, KDSETMODE, KD_TEXT);
		}

		/* Head back over to the original virtual terminal */
		if (saved_vt > 0) {
			ioctl(keyboard_fd, VT_ACTIVATE, saved_vt);
		}

		in_graphics_mode = 0;
	}
}

/**
 *
 */
void GC_CloseKeyboard(_THIS)
{
	if (keyboard_fd >= 0) {
		GC_LeaveGraphicsMode(_this);
		if (keyboard_fd > 0) {
			close(keyboard_fd);
		}
	}
	keyboard_fd = -1;
}

/**
 *
 */
int GC_OpenKeyboard(_THIS)
{
	/* Open only if not already opened */
	if (keyboard_fd < 0) {
		static const char *const tty0[] =
		    { "/dev/tty0", "/dev/vc/0", NULL };
		static const char *const vcs[] =
		    { "/dev/vc/%d", "/dev/tty%d", NULL };
		int i, tty0_fd;

		/* Try to query for a free virtual terminal */
		tty0_fd = -1;
		for (i = 0; tty0[i] && (tty0_fd < 0); ++i) {
			tty0_fd = open(tty0[i], O_WRONLY, 0);
		}
		if (tty0_fd < 0) {
			tty0_fd = dup(0);	/* Maybe stdin is a VT? */
		}
		ioctl(tty0_fd, VT_OPENQRY, &current_vt);
		close(tty0_fd);
		if ((geteuid() == 0) && (current_vt > 0)) {
			for (i = 0; vcs[i] && (keyboard_fd < 0); ++i) {
				char vtpath[12];

				sprintf(vtpath, vcs[i], current_vt);
				keyboard_fd = open(vtpath, O_RDWR, 0);
#ifdef DEBUG_KEYBOARD
				fprintf(stderr, "vtpath = %s, fd = %d\n",
					vtpath, keyboard_fd);
#endif				/* DEBUG_KEYBOARD */

				/* This needs to be our controlling tty
				   so that the kernel ioctl() calls work
				 */
				if (keyboard_fd >= 0) {
					tty0_fd = open("/dev/tty", O_RDWR, 0);
					if (tty0_fd >= 0) {
						ioctl(tty0_fd, TIOCNOTTY, 0);
						close(tty0_fd);
					}
				}
			}
		}

		if (keyboard_fd < 0) {
			/* Last resort, maybe our tty is a usable VT */
			current_vt = 0;
			keyboard_fd = open("/dev/tty", O_RDWR);
		}
#ifdef DEBUG_KEYBOARD
		fprintf(stderr, "Current VT: %d\n", current_vt);
#endif
	}
	return (keyboard_fd);
}

/**
 *
 */
void GC_PumpEvents(_THIS)
{
}

/**
 *
 */
void GC_InitOSKeymap(_THIS)
{
}
