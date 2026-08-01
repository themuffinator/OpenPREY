/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../renderer/tr_local.h"
#include "linux_shared.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(OPENQ4_HAVE_X11_HELPERS)
#include <X11/Xlib.h>
extern "C" {
#include "libXNVCtrl/NVCtrlLib.h"
}
#endif

#define OPENQ4_SDL3_LINUX_HOST 1
#include "../sdl3/sdl3_backend.cpp"

#if defined(OPENQ4_HAVE_X11_HELPERS)
Display *dpy = NULL;
Window win = 0;
#endif
bool dga_found = false;

idCVar sys_videoRam(
	"sys_videoRam",
	"0",
	CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_INTEGER,
	"Texture memory on the video card (in megabytes) - 0: autodetect",
	0,
	OPENQ4_LINUX_MAX_CONFIGURED_VIDEO_RAM_MB
);

bool QGL_Init(const char *dllname) {
	(void)dllname;
	return true;
}

void QGL_Shutdown(void) {
}

static bool Sys_ReadUnsignedLongLongFile(const char *path, unsigned long long &value) {
	value = 0;

	const int fd = open(path, O_RDONLY);
	if (fd == -1) {
		return false;
	}

	char buffer[64];
	const int len = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);

	if (len <= 0) {
		return false;
	}

	buffer[len] = '\0';
	char *end = NULL;
	errno = 0;
	const unsigned long long parsed = strtoull(buffer, &end, 10);
	if (errno != 0 || end == buffer || parsed == 0) {
		return false;
	}

	value = parsed;
	return true;
}

static void Sys_UpdateLargestDrmSysfsVideoRamBytes(const char *nodeName, unsigned long long &largestBytes) {
	if (nodeName == NULL || nodeName[0] == '\0') {
		return;
	}
	if (idStr::Cmpn(nodeName, "card", 4) != 0 && idStr::Cmpn(nodeName, "renderD", 7) != 0) {
		return;
	}

	char path[192];
	idStr::snPrintf(path, sizeof(path), "/sys/class/drm/%s/device/mem_info_vram_total", nodeName);

	unsigned long long bytes = 0;
	if (Sys_ReadUnsignedLongLongFile(path, bytes) && bytes > largestBytes) {
		largestBytes = bytes;
	}
}

static bool Sys_QueryEnumeratedDrmSysfsVideoRamBytes(unsigned long long &largestBytes) {
	DIR *dir = opendir("/sys/class/drm");
	if (dir == NULL) {
		return false;
	}

	for (;;) {
		errno = 0;
		struct dirent *entry = readdir(dir);
		if (entry == NULL) {
			break;
		}
		Sys_UpdateLargestDrmSysfsVideoRamBytes(entry->d_name, largestBytes);
	}

	const bool readComplete = errno == 0;
	closedir(dir);
	return readComplete;
}

static void Sys_QueryKnownDrmSysfsVideoRamBytes(unsigned long long &largestBytes) {
	for (int card = 0; card < 16; ++card) {
		char nodeName[32];
		idStr::snPrintf(nodeName, sizeof(nodeName), "card%d", card);
		Sys_UpdateLargestDrmSysfsVideoRamBytes(nodeName, largestBytes);
	}

	for (int renderNode = 128; renderNode < 144; ++renderNode) {
		char nodeName[32];
		idStr::snPrintf(nodeName, sizeof(nodeName), "renderD%d", renderNode);
		Sys_UpdateLargestDrmSysfsVideoRamBytes(nodeName, largestBytes);
	}
}

static int Sys_QueryDrmSysfsVideoRamMB(void) {
	unsigned long long largestBytes = 0;
	const bool enumeratedDrm = Sys_QueryEnumeratedDrmSysfsVideoRamBytes(largestBytes);
	if (!enumeratedDrm || largestBytes == 0) {
		Sys_QueryKnownDrmSysfsVideoRamBytes(largestBytes);
	}

	if (largestBytes == 0) {
		return 0;
	}

	const unsigned long long megabytes = largestBytes / (1024ULL * 1024ULL);
	if (megabytes == 0) {
		return 0;
	}
	if (megabytes > static_cast<unsigned long long>(OPENQ4_LINUX_MAX_CONFIGURED_VIDEO_RAM_MB)) {
		return OPENQ4_LINUX_MAX_CONFIGURED_VIDEO_RAM_MB;
	}
	return static_cast<int>(megabytes);
}

static bool Sys_TryDrmSysfsVideoRam(int &cachedVideoRam) {
	cachedVideoRam = Sys_QueryDrmSysfsVideoRamMB();
	if (cachedVideoRam > 0) {
		common->Printf("found DRM sysfs VRAM total: %d MB\n", cachedVideoRam);
		return true;
	}
	return false;
}

static bool Sys_IsWaylandVideoDriverName(const char *driverName) {
	return driverName != NULL && driverName[0] != '\0' && idStr::Icmp(driverName, "wayland") == 0;
}

static bool Sys_PreferDrmSysfsBeforeX11VideoRam(void) {
	const char *currentVideoDriver = SDL_GetCurrentVideoDriver();
	if (Sys_IsWaylandVideoDriverName(currentVideoDriver) || SDL3_IsNativeWaylandVideoDriver()) {
		return true;
	}

	const bool waylandSession = SDL3_EnvHasValue("WAYLAND_DISPLAY");
	const bool explicitX11Fallback = SDL3_EnvFlagEnabledWithLegacyAlias("OPENPREY_FORCE_X11", "OPENQ4_FORCE_X11") ||
		SDL3_StringEquals(getenv("SDL_VIDEO_DRIVER"), "x11") ||
		SDL3_StringEquals(getenv("SDL_VIDEODRIVER"), "x11");
	return (waylandSession && !explicitX11Fallback) ||
		Sys_IsWaylandVideoDriverName(getenv("SDL_VIDEO_DRIVER")) ||
		Sys_IsWaylandVideoDriverName(getenv("SDL_VIDEODRIVER"));
}

bool Sys_GetDesktopResolution(int *width, int *height) {
	return SDL3_QueryDesktopResolution(width, height, "SDL3 Linux");
}

int Sys_GetVideoRam(void) {
	static int cachedVideoRam = 0;
	if (cachedVideoRam != 0) {
		return cachedVideoRam;
	}

	if (sys_videoRam.GetInteger() > 0) {
		cachedVideoRam = sys_videoRam.GetInteger();
		return cachedVideoRam;
	}

	common->Printf("guessing video ram ( use +set sys_videoRam to force ) ..\n");

	const bool preferDrmBeforeX11 = Sys_PreferDrmSysfsBeforeX11VideoRam();
	bool triedDrmSysfsVideoRam = preferDrmBeforeX11;
	if (preferDrmBeforeX11 && Sys_TryDrmSysfsVideoRam(cachedVideoRam)) {
		return cachedVideoRam;
	}

#if defined(OPENQ4_HAVE_X11_HELPERS)
	if (!preferDrmBeforeX11) {
		Display *queryDisplay = dpy;
		const bool ownsDisplay = (queryDisplay == NULL);
		if (queryDisplay == NULL && getenv("DISPLAY") != NULL) {
			queryDisplay = XOpenDisplay(NULL);
		}

		if (queryDisplay != NULL) {
			const int screen = DefaultScreen(queryDisplay);
			int major = 0;
			int minor = 0;
			int value = 0;

			if (XNVCTRLQueryVersion(queryDisplay, &major, &minor)) {
				common->Printf("found XNVCtrl extension %d.%d\n", major, minor);
				if (XNVCTRLIsNvScreen(queryDisplay, screen) &&
					XNVCTRLQueryAttribute(queryDisplay, screen, 0, NV_CTRL_VIDEO_RAM, &value)) {
					cachedVideoRam = value / 1024;
				}
			}

			if (ownsDisplay) {
				XCloseDisplay(queryDisplay);
			}

			if (cachedVideoRam > 0) {
				return cachedVideoRam;
			}
		}
	}
#endif

	if (!triedDrmSysfsVideoRam && Sys_TryDrmSysfsVideoRam(cachedVideoRam)) {
		return cachedVideoRam;
	}

	const int fd = open("/proc/dri/0/umm", O_RDONLY);
	if (fd != -1) {
		char ummBuffer[1024];
		const int len = read(fd, ummBuffer, sizeof(ummBuffer));
		close(fd);

		if (len > 1) {
			ummBuffer[len - 1] = '\0';
			int total = 0;
			for (char *line = strtok(ummBuffer, "\n"); line != NULL; line = strtok(NULL, "\n")) {
				if (strlen(line) >= 13 && strstr(line, "max   LFB =") == line) {
					total += atoi(line + 12);
				} else if (strlen(line) >= 13 && strstr(line, "max   Inv =") == line) {
					total += atoi(line + 12);
				}
			}
			if (total > 0) {
				cachedVideoRam = (total / 1048576) & ~15;
				return cachedVideoRam;
			}
		} else if (len == -1) {
			common->Printf("read /proc/dri/0/umm failed: %s\n", strerror(errno));
		}
	}

	common->Printf("guess failed, using conservative modern VRAM setting ( %dMB VRAM )\n", OPENQ4_LINUX_UNKNOWN_VIDEO_RAM_MB);
	cachedVideoRam = OPENQ4_LINUX_UNKNOWN_VIDEO_RAM_MB;
	return cachedVideoRam;
}
