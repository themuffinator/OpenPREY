/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

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

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#include "../../idlib/precompiled.h"
#include "posix_public.h"
#include "../../renderer/RendererStartupDiagnostics.h"
#include "../../framework/GameModuleDiagnostics.h"

// module-only clients carry no renderer TUs; the crash breadcrumb loses the
// startup-phase detail rather than reaching into the module from a signal
// handler
static const char *Posix_RendererStartupPhaseName( void ) {
#ifdef OPENQ4_RENDERER_MODULE_ONLY
	return "unavailable (module renderer)";
#else
	return R_RendererStartupPhaseSignalName();
#endif
}

#include <string.h>
#include <errno.h>
#include <unistd.h>

struct posixSignalRoute_t {
	int signum;
	const char *name;
	bool graceful;
};

static const posixSignalRoute_t signalRoutes[] = {
	{ SIGHUP, "SIGHUP", true },
	{ SIGINT, "SIGINT", true },
	{ SIGTERM, "SIGTERM", true },
	{ SIGQUIT, "SIGQUIT", true },
	{ SIGILL, "SIGILL", false },
	{ SIGTRAP, "SIGTRAP", false },
#if defined( SIGIOT ) && SIGIOT != SIGABRT
	{ SIGIOT, "SIGIOT", false },
#endif
	{ SIGBUS, "SIGBUS", false },
	{ SIGFPE, "SIGFPE", false },
	{ SIGSEGV, "SIGSEGV", false },
	{ SIGABRT, "SIGABRT", false },
	{ -1, NULL, false }
};

// Sized to match POSIX_CONSOLE_FATAL_SIZE (posix_syscon.cpp) so the fatal
// message is not truncated on its way to the console fatal buffer.
static char fatalError[ 4096 ];
static volatile sig_atomic_t pendingQuitSignal = 0;
static volatile sig_atomic_t activeFatalSignal = 0;

static const posixSignalRoute_t *Posix_FindSignalRoute( int signum ) {
	for ( int i = 0; signalRoutes[ i ].signum != -1; ++i ) {
		if ( signalRoutes[ i ].signum == signum ) {
			return &signalRoutes[ i ];
		}
	}
	return NULL;
}

const char *Posix_SignalName( int signum ) {
	const posixSignalRoute_t *route = Posix_FindSignalRoute( signum );
	if ( route != NULL ) {
		return route->name;
	}
	return "unknown signal";
}

static void Posix_WriteSignalText( const char *text ) {
	if ( text == NULL ) {
		return;
	}

	size_t length = 0;
	while ( text[ length ] != '\0' ) {
		length++;
	}
	if ( length > 0 ) {
		write( STDERR_FILENO, text, length );
	}
}

static void Posix_WriteSignalNumber( int value ) {
	char buffer[ 16 ];
	int pos = sizeof( buffer );
	unsigned int number;

	if ( value < 0 ) {
		Posix_WriteSignalText( "-" );
		number = static_cast<unsigned int>( -( value + 1 ) ) + 1U;
	} else {
		number = static_cast<unsigned int>( value );
	}

	do {
		buffer[ --pos ] = static_cast<char>( '0' + ( number % 10 ) );
		number /= 10;
	} while ( number > 0 && pos > 0 );

	write( STDERR_FILENO, buffer + pos, sizeof( buffer ) - pos );
}

/*
================
Posix_ClearSigs
================
*/
void Posix_ClearSigs( ) {
	struct sigaction action;
	
	/* Set up the structure */
	action.sa_handler = SIG_DFL;
	sigemptyset( &action.sa_mask );
	action.sa_flags = 0;

	for ( int i = 0; signalRoutes[ i ].signum != -1; ++i ) {
		if ( sigaction( signalRoutes[ i ].signum, &action, NULL ) != 0 ) {
			Sys_Printf( "Failed to reset %s handler: %s\n", signalRoutes[ i ].name, strerror( errno ) );
		}
	}
	if ( sigaction( SIGPIPE, &action, NULL ) != 0 ) {
		Sys_Printf( "Failed to reset SIGPIPE handler: %s\n", strerror( errno ) );
	}
}

/*
================
sig_handler
================
*/
static void sig_handler( int signum, siginfo_t *info, void *context ) {
	(void)info;
	(void)context;

	const posixSignalRoute_t *route = Posix_FindSignalRoute( signum );
	if ( route != NULL && route->graceful ) {
		pendingQuitSignal = signum;
		Posix_WriteSignalText( "openPREY: received " );
		Posix_WriteSignalText( route->name );
		Posix_WriteSignalText( ", requesting shutdown\n" );
		return;
	}

	if ( activeFatalSignal != 0 ) {
		Posix_WriteSignalText( "openPREY: double fault while handling " );
		Posix_WriteSignalText( Posix_SignalName( static_cast<int>( activeFatalSignal ) ) );
		Posix_WriteSignalText( "; second signal " );
		Posix_WriteSignalText( Posix_SignalName( signum ) );
		Posix_WriteSignalText( ", bailing out\n" );
		_exit( 128 + signum );
	}

	activeFatalSignal = signum;
	Posix_WriteSignalText( "openPREY: fatal signal " );
	Posix_WriteSignalText( Posix_SignalName( signum ) );
	Posix_WriteSignalText( " (" );
	Posix_WriteSignalNumber( signum );
	Posix_WriteSignalText( "), exiting without unsafe engine shutdown\n" );
	Posix_WriteSignalText( "openPREY: last renderer startup phase: " );
	Posix_WriteSignalText( Posix_RendererStartupPhaseName() );
	Posix_WriteSignalText( "\n" );
	Posix_WriteSignalText( "openPREY: last game module phase: " );
	Posix_WriteSignalText( Com_GameModuleLoadPhaseSignalName() );
	Posix_WriteSignalText( "\n" );

	// Mirror the stderr breadcrumb into the save-path fatal file so support
	// bundles capture signal deaths. Pure memory ops plus the raw
	// open/write/close appender keep this async-signal-safe.
	char breadcrumb[ 448 ];
	strcpy( breadcrumb, "fatal signal " );
	strcat( breadcrumb, Posix_SignalName( signum ) );
	strcat( breadcrumb, "; last renderer startup phase: " );
	strcat( breadcrumb, Posix_RendererStartupPhaseName() );
	strcat( breadcrumb, "; last game module phase: " );
	strcat( breadcrumb, Com_GameModuleLoadPhaseSignalName() );
	Posix_AppendFatalBreadcrumbRaw( breadcrumb );

	_exit( 128 + signum );
}

/*
================
Posix_InitSigs
================
*/
void Posix_InitSigs( ) {
	struct sigaction action;
	struct sigaction ignoreAction;

	fatalError[0] = '\0';
	pendingQuitSignal = 0;
	activeFatalSignal = 0;
	
	/* Set up the structure */
	action.sa_sigaction = sig_handler;
	sigemptyset( &action.sa_mask );
	action.sa_flags = SA_SIGINFO;

	for ( int i = 0; signalRoutes[ i ].signum != -1; ++i ) {
		if ( sigaction( signalRoutes[ i ].signum, &action, NULL ) != 0 ) {
			Sys_Printf( "Failed to set %s handler: %s\n", signalRoutes[ i ].name, strerror( errno ) );
		}
	}

	memset( &ignoreAction, 0, sizeof( ignoreAction ) );
	ignoreAction.sa_handler = SIG_IGN;
	sigemptyset( &ignoreAction.sa_mask );
	if ( sigaction( SIGPIPE, &ignoreAction, NULL ) != 0 ) {
		Sys_Printf( "Failed to ignore SIGPIPE: %s\n", strerror( errno ) );
	}

	// if the process is backgrounded (running non interactively)
	// then SIGTTIN or SIGTOU could be emitted, if not caught, turns into a SIGSTP
	signal( SIGTTIN, SIG_IGN );
	signal( SIGTTOU, SIG_IGN );	
}

/*
========================
Posix_ConsumeQuitSignal
========================
*/
int Posix_ConsumeQuitSignal( ) {
	const int signum = static_cast<int>( pendingQuitSignal );
	if ( signum != 0 ) {
		pendingQuitSignal = 0;
	}
	return signum;
}

/*
==================
Sys_SetFatalError
==================
*/
void Sys_SetFatalError( const char *error ) {
	if ( error == NULL ) {
		fatalError[0] = '\0';
		Posix_ConsoleSetFatalError( NULL );
		return;
	}

	strncpy( fatalError, error, sizeof( fatalError ) - 1 );
	fatalError[sizeof( fatalError ) - 1] = '\0';
	Posix_ConsoleSetFatalError( fatalError );
}
