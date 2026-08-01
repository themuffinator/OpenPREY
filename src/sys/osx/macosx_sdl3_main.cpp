/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code. See docs/legal for details.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../../framework/Common.h"
#include "../posix/posix_public.h"
#include "../sys_public.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>

static void Sys_HandlePendingQuitSignal(void) {
	const int quitSignal = Posix_ConsumeQuitSignal();
	if (quitSignal == 0) {
		return;
	}

	Posix_SetExit(128 + quitSignal);
	common->Printf("Exiting on %s\n", Posix_SignalName(quitSignal));
	common->Quit();
}

static int SDLCALL OpenQ4_Main(int argc, char **argv) {
	Posix_EarlyInit();
#ifndef ID_DEDICATED
	Sys_ShowSplash();
#endif

	if (argc > 1 && argv != NULL) {
		common->Init(argc - 1, const_cast<const char **>(&argv[1]), NULL);
	} else {
		common->Init(0, NULL, NULL);
	}

#ifndef ID_DEDICATED
	Sys_DestroySplash();
#endif
	Sys_HandlePendingQuitSignal();
	Posix_LateInit();

	while (1) {
		Sys_HandlePendingQuitSignal();
		common->Frame();
	}

	return 0;
}

int main(int argc, char **argv) {
	static char emptyArg0[] = "openPREY";
	char *emptyArgv[] = { emptyArg0, NULL };
	if (argc <= 0 || argv == NULL) {
		argc = 1;
		argv = emptyArgv;
	}
	return SDL_RunApp(argc, argv, OpenQ4_Main, NULL);
}
