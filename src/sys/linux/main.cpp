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
#include "../posix/posix_public.h"
#include "../sys_local.h"

#include <pthread.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#ifdef ID_MCHECK
#include <mcheck.h>
#endif

#ifndef LINUX_DEFAULT_PATH
#define LINUX_DEFAULT_PATH "/usr/local/games/openprey"
#endif

static idStr	basepath;
static idStr	savepath;

static const int MAX_PROCESS_ARGS = 32;
static const int MAX_PROCESS_COMMAND = 4096;

static bool Sys_StringHasControlCharacters( const char *text ) {
	if ( text == NULL ) {
		return true;
	}

	for ( const unsigned char *scan = reinterpret_cast<const unsigned char *>( text ); *scan != '\0'; ++scan ) {
		if ( iscntrl( *scan ) ) {
			return true;
		}
	}
	return false;
}

static bool Sys_IsRegularFile( const char *path ) {
	struct stat st;
	return path != NULL && path[0] != '\0' && stat( path, &st ) == 0 && S_ISREG( st.st_mode );
}

static void Sys_EnsureOwnerExecutable( const char *path ) {
	struct stat st;
	if ( path == NULL || path[0] == '\0' || stat( path, &st ) == -1 || !S_ISREG( st.st_mode ) ) {
		return;
	}
	if ( ( st.st_mode & S_IXUSR ) == 0 && chmod( path, st.st_mode | S_IXUSR ) == -1 ) {
		Sys_Printf( "chmod +x %s failed: %s\n", path, strerror( errno ) );
	}
}

static void Sys_WriteProcessChildText( const char *text ) {
	if ( text == NULL ) {
		return;
	}

	size_t length = 0;
	while ( text[length] != '\0' ) {
		++length;
	}
	if ( length > 0 ) {
		write( STDERR_FILENO, text, length );
	}
}

static bool Sys_ParseProcessCommandLine( const char *command, char *buffer, size_t bufferSize, char **argv, int maxArgv ) {
	if ( command == NULL || command[0] == '\0' || buffer == NULL || bufferSize == 0 || argv == NULL || maxArgv < 2 ) {
		return false;
	}
	if ( strlen( command ) >= bufferSize || Sys_StringHasControlCharacters( command ) ) {
		return false;
	}

	const char *read = command;
	char *write = buffer;
	char *end = buffer + bufferSize - 1;
	int argc = 0;

	while ( *read != '\0' ) {
		while ( *read != '\0' && isspace( static_cast<unsigned char>( *read ) ) ) {
			++read;
		}
		if ( *read == '\0' ) {
			break;
		}
		if ( argc >= maxArgv - 1 ) {
			return false;
		}

		argv[argc++] = write;
		char quote = '\0';
		while ( *read != '\0' ) {
			const char ch = *read;
			if ( quote == '\0' && isspace( static_cast<unsigned char>( ch ) ) ) {
				break;
			}
			if ( ch == '"' || ch == '\'' ) {
				if ( quote == '\0' ) {
					quote = ch;
					++read;
					continue;
				}
				if ( quote == ch ) {
					quote = '\0';
					++read;
					continue;
				}
			}
			if ( ch == '\\' && read[1] != '\0' ) {
				++read;
			}
			if ( write >= end ) {
				return false;
			}
			*write++ = *read++;
		}

		if ( quote != '\0' ) {
			return false;
		}
		if ( write >= end ) {
			return false;
		}
		*write++ = '\0';
	}

	argv[argc] = NULL;
	return argc > 0 && argv[0][0] != '\0';
}

static void Sys_AppendQuotedProcessArg( idStr &command, const char *arg ) {
	if ( command.Length() > 0 ) {
		command += " ";
	}
	command += "\"";
	for ( const char *scan = arg; scan != NULL && *scan != '\0'; ++scan ) {
		if ( *scan == '"' || *scan == '\\' ) {
			command += "\\";
		}
		char text[2] = { *scan, '\0' };
		command += text;
	}
	command += "\"";
}

static idStr Sys_BuildProcessCommandLine( char *const argv[] ) {
	idStr command;
	for ( int i = 0; argv != NULL && argv[i] != NULL; ++i ) {
		Sys_AppendQuotedProcessArg( command, argv[i] );
	}
	return command;
}

static bool Sys_ExecProcessArgs( char *const argv[], bool dofork ) {
	if ( argv == NULL || argv[0] == NULL || argv[0][0] == '\0' ) {
		return false;
	}

	Sys_EnsureOwnerExecutable( argv[0] );

	if ( dofork ) {
		const pid_t child = fork();
		if ( child == -1 ) {
			Sys_Printf( "fork failed: %s\n", strerror( errno ) );
			return false;
		}
		if ( child != 0 ) {
			return true;
		}
		execvp( argv[0], argv );
		Sys_WriteProcessChildText( "openPREY child exec failed: " );
		Sys_WriteProcessChildText( argv[0] );
		Sys_WriteProcessChildText( "\n" );
		_exit( 127 );
	}

	Sys_Printf( "exec %s\n", argv[0] );
	execvp( argv[0], argv );
	Sys_Printf( "exec %s failed: %s\n", argv[0], strerror( errno ) );
	_exit( 127 );
}

static bool Sys_QueueOrStartProcessArgs( char *const argv[], bool quit ) {
	if ( argv == NULL || argv[0] == NULL || argv[0][0] == '\0' ) {
		return false;
	}

	if ( quit ) {
		const idStr command = Sys_BuildProcessCommandLine( argv );
		common->DPrintf( "Sys_StartProcess %s (delaying until final exit)\n", command.c_str() );
		Posix_SetExitSpawn( command.c_str() );
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
		return true;
	}

	return Sys_ExecProcessArgs( argv, true );
}

static bool Sys_IsSafeURL( const char *url ) {
	if ( url == NULL || url[0] == '\0' || Sys_StringHasControlCharacters( url ) ) {
		return false;
	}
	if ( !isalpha( static_cast<unsigned char>( url[0] ) ) ) {
		return false;
	}
	for ( const char *scan = url + 1; *scan != '\0'; ++scan ) {
		if ( *scan == ':' ) {
			return true;
		}
		if ( !( isalnum( static_cast<unsigned char>( *scan ) ) || *scan == '+' || *scan == '-' || *scan == '.' ) ) {
			return false;
		}
	}
	return false;
}

static bool Sys_FindExecutableOnPath( const char *name, idStr &resolvedPath ) {
	resolvedPath.Clear();
	if ( name == NULL || name[0] == '\0' ) {
		return false;
	}

	if ( strchr( name, '/' ) != NULL ) {
		if ( access( name, X_OK ) == 0 ) {
			resolvedPath = name;
			return true;
		}
		return false;
	}

	const char *pathEnv = getenv( "PATH" );
	if ( pathEnv == NULL || pathEnv[0] == '\0' ) {
		return false;
	}

	idStr pathList = pathEnv;
	int start = 0;
	while ( start <= pathList.Length() ) {
		const int separator = pathList.Find( ':', start );
		const int length = ( separator >= 0 ) ? separator - start : pathList.Length() - start;
		idStr directory = pathList.Mid( start, length );
		if ( directory.Length() == 0 ) {
			directory = ".";
		}

		idStr candidate = directory;
		candidate.AppendPath( name );
		if ( access( candidate.c_str(), X_OK ) == 0 ) {
			resolvedPath = candidate;
			return true;
		}

		if ( separator < 0 ) {
			break;
		}
		start = separator + 1;
	}

	return false;
}

static void Sys_AddUniqueCpuInfoValue( idStrList &values, const idStr &value ) {
	if ( value.Length() <= 0 ) {
		return;
	}

	for ( int i = 0; i < values.Num(); ++i ) {
		if ( values[ i ].Icmp( value.c_str() ) == 0 ) {
			return;
		}
	}

	values.Append( value );
}

static void Sys_FinalizeLinuxCpuInfoBlock( const idStr &physicalId, const idStr &coreId, idStrList &packageIds, idStrList &coreIds ) {
	if ( physicalId.Length() > 0 ) {
		Sys_AddUniqueCpuInfoValue( packageIds, physicalId );
		if ( coreId.Length() > 0 ) {
			Sys_AddUniqueCpuInfoValue( coreIds, physicalId + ":" + coreId );
		}
	}
}

static bool Sys_ReadLinuxCpuInfo( idStr &cpuInfoText ) {
	char buffer[ 4096 ];
	int fd;
	int bytesRead;
	bool readFailed = false;

	cpuInfoText.Clear();

	fd = open( "/proc/cpuinfo", O_RDONLY );
	if ( fd == -1 ) {
		return false;
	}

	while ( ( bytesRead = read( fd, buffer, sizeof( buffer ) ) ) > 0 ) {
		cpuInfoText.Append( buffer, bytesRead );
	}
	if ( bytesRead < 0 ) {
		readFailed = true;
	}

	close( fd );

	return !readFailed && cpuInfoText.Length() > 0;
}

static bool Sys_GetLinuxProcessorInfo( idStr &processorName, int &logicalCount, int &physicalCount, int &packageCount ) {
	idStr cpuInfoText;
	idStr currentCoreId;
	idStr currentPhysicalId;
	idStrList coreIds;
	idStrList packageIds;
	int fallbackCpuCores = 0;
	int fallbackSiblings = 0;
	int start = 0;

	processorName.Clear();
	logicalCount = static_cast<int>( sysconf( _SC_NPROCESSORS_ONLN ) );
	physicalCount = 0;
	packageCount = 0;

	if ( !Sys_ReadLinuxCpuInfo( cpuInfoText ) ) {
		if ( logicalCount > 0 ) {
			physicalCount = logicalCount;
			packageCount = 1;
		}
		return logicalCount > 0;
	}

	while ( start <= cpuInfoText.Length() ) {
		idStr key;
		idStr line;
		idStr value;
		const int end = cpuInfoText.Find( '\n', start );
		const int lineLength = ( end >= 0 ) ? end - start : cpuInfoText.Length() - start;

		line = cpuInfoText.Mid( start, lineLength );
		line.StripTrailing( '\r' );
		line.StripTrailingWhitespace();

		if ( line.Length() == 0 ) {
			Sys_FinalizeLinuxCpuInfoBlock( currentPhysicalId, currentCoreId, packageIds, coreIds );
			currentPhysicalId.Clear();
			currentCoreId.Clear();
		} else {
			const int delimiter = line.Find( ':' );
			if ( delimiter >= 0 ) {
				key = line.Left( delimiter );
				value = line.Mid( delimiter + 1, line.Length() - delimiter - 1 );
				key.StripTrailingWhitespace();
				value.StripLeading( ' ' );
				value.StripLeading( '\t' );
				value.StripTrailingWhitespace();

				if ( processorName.Length() == 0
					&& ( key.Icmp( "model name" ) == 0 || key.Icmp( "Hardware" ) == 0 || key.Icmp( "Processor" ) == 0 )
					&& value.Length() > 0 ) {
					processorName = value;
				} else if ( key.Icmp( "physical id" ) == 0 ) {
					currentPhysicalId = value;
				} else if ( key.Icmp( "core id" ) == 0 ) {
					currentCoreId = value;
				} else if ( key.Icmp( "cpu cores" ) == 0 && fallbackCpuCores <= 0 ) {
					fallbackCpuCores = atoi( value.c_str() );
				} else if ( key.Icmp( "siblings" ) == 0 && fallbackSiblings <= 0 ) {
					fallbackSiblings = atoi( value.c_str() );
				}
			}
		}

		if ( end < 0 ) {
			break;
		}
		start = end + 1;
	}

	Sys_FinalizeLinuxCpuInfoBlock( currentPhysicalId, currentCoreId, packageIds, coreIds );

	if ( packageIds.Num() > 0 ) {
		packageCount = packageIds.Num();
	}

	if ( coreIds.Num() > 0 ) {
		physicalCount = coreIds.Num();
	} else if ( fallbackCpuCores > 0 ) {
		physicalCount = fallbackCpuCores * Max( 1, packageCount );
	}

	if ( logicalCount <= 0 && fallbackSiblings > 0 ) {
		logicalCount = fallbackSiblings * Max( 1, packageCount );
	}

	if ( logicalCount <= 0 ) {
		logicalCount = physicalCount;
	}

	if ( packageCount <= 0 && physicalCount > 0 ) {
		packageCount = 1;
	}

	return processorName.Length() > 0 || logicalCount > 0 || physicalCount > 0;
}

static int Sys_RoundSystemRamMegabytes( unsigned long long bytes, int fallbackMegabytes ) {
	if ( bytes == 0 ) {
		return fallbackMegabytes;
	}

	unsigned long long megabytes = bytes / ( 1024ULL * 1024ULL );
	megabytes = ( megabytes + 8ULL ) & ~15ULL;
	if ( megabytes > static_cast<unsigned long long>( idMath::INT_MAX ) ) {
		return idMath::INT_MAX;
	}
	return static_cast<int>( megabytes );
}

/*
==========================
Sys_ReportWaylandRuntime
==========================
*/
static void Sys_ReportWaylandRuntime( void ) {
#if defined( ID_DEDICATED )
	return;
#endif

	const char *waylandDisplay = getenv( "WAYLAND_DISPLAY" );
	const char *x11Display = getenv( "DISPLAY" );
	const char *sdlVideoDriver = getenv( "SDL_VIDEO_DRIVER" );
	const char *legacySdlVideoDriver = getenv( "SDL_VIDEODRIVER" );
	const char *openPreyForceX11 = getenv( "OPENPREY_FORCE_X11" );
	const char *openPreyDisableLibdecor = getenv( "OPENPREY_WAYLAND_DISABLE_LIBDECOR" );
	const char *openPreyPreferLibdecor = getenv( "OPENPREY_WAYLAND_PREFER_LIBDECOR" );
	const char *openPreySyncWindowOps = getenv( "OPENPREY_WAYLAND_SYNC_WINDOW_OPS" );
	// Temporary migration aliases for environments configured before the rebase.
	if ( openPreyForceX11 == NULL || openPreyForceX11[0] == '\0' ) {
		openPreyForceX11 = getenv( "OPENQ4_FORCE_X11" );
	}
	if ( openPreyDisableLibdecor == NULL || openPreyDisableLibdecor[0] == '\0' ) {
		openPreyDisableLibdecor = getenv( "OPENQ4_WAYLAND_DISABLE_LIBDECOR" );
	}
	if ( openPreyPreferLibdecor == NULL || openPreyPreferLibdecor[0] == '\0' ) {
		openPreyPreferLibdecor = getenv( "OPENQ4_WAYLAND_PREFER_LIBDECOR" );
	}
	if ( openPreySyncWindowOps == NULL || openPreySyncWindowOps[0] == '\0' ) {
		openPreySyncWindowOps = getenv( "OPENQ4_WAYLAND_SYNC_WINDOW_OPS" );
	}
	const char *effectiveSdlVideoDriver = ( sdlVideoDriver != NULL && sdlVideoDriver[0] != '\0' )
		? sdlVideoDriver
		: legacySdlVideoDriver;
	const char *effectiveOpenPreyForceX11 = ( openPreyForceX11 != NULL && openPreyForceX11[0] != '\0' )
		? openPreyForceX11
		: "<unset>";
	const char *effectiveOpenPreyDisableLibdecor = ( openPreyDisableLibdecor != NULL && openPreyDisableLibdecor[0] != '\0' )
		? openPreyDisableLibdecor
		: "<unset>";
	const char *effectiveOpenPreyPreferLibdecor = ( openPreyPreferLibdecor != NULL && openPreyPreferLibdecor[0] != '\0' )
		? openPreyPreferLibdecor
		: "<unset>";
	const char *effectiveOpenPreySyncWindowOps = ( openPreySyncWindowOps != NULL && openPreySyncWindowOps[0] != '\0' )
		? openPreySyncWindowOps
		: "<unset>";

	if ( waylandDisplay == NULL || waylandDisplay[0] == '\0' ) {
		return;
	}

	if ( x11Display == NULL || x11Display[0] == '\0' ) {
#if defined( USE_SDL3 )
		Sys_Printf(
			"Wayland session detected (%s) without X11 DISPLAY. "
			"openPREY will use SDL3's native Wayland path when selected; "
			"use OPENPREY_FORCE_X11=1 from an XWayland-enabled session for fallback, "
			"OPENPREY_WAYLAND_DISABLE_LIBDECOR=1 to bypass libdecor issues, "
			"OPENPREY_WAYLAND_PREFER_LIBDECOR=1 for decoration issues, or "
			"OPENPREY_WAYLAND_SYNC_WINDOW_OPS=1 for window-operation diagnostics.\n",
			waylandDisplay
		);
#else
		Sys_Printf(
			"Wayland session detected (%s) without X11 DISPLAY. "
			"openPREY currently requires X11/GLX on Linux; launch from an XWayland-enabled session.\n",
			waylandDisplay
		);
#endif
		return;
	}

#if defined( USE_SDL3 )
	Sys_Printf(
		"Wayland session detected (%s) with X11 DISPLAY=%s available. SDL3 video override: %s; "
		"OPENPREY_FORCE_X11=%s OPENPREY_WAYLAND_DISABLE_LIBDECOR=%s OPENPREY_WAYLAND_PREFER_LIBDECOR=%s OPENPREY_WAYLAND_SYNC_WINDOW_OPS=%s.\n",
		waylandDisplay,
		x11Display,
		( effectiveSdlVideoDriver != NULL && effectiveSdlVideoDriver[0] != '\0' ) ? effectiveSdlVideoDriver : "<unset>",
		effectiveOpenPreyForceX11,
		effectiveOpenPreyDisableLibdecor,
		effectiveOpenPreyPreferLibdecor,
		effectiveOpenPreySyncWindowOps
	);
#else
	Sys_Printf(
		"Wayland session detected (%s). openPREY is using X11 via XWayland (DISPLAY=%s).\n",
		waylandDisplay,
		x11Display
	);
#endif
}

/*
===========
Sys_InitScanTable
===========
*/
#if !defined(USE_SDL3)
void Sys_InitScanTable( void ) {
	common->DPrintf( "TODO: Sys_InitScanTable\n" );
}
#endif

/*
=================
Sys_AsyncThread
=================
*/
void Sys_AsyncThread( void ) {
	while (1) {
		if ( Sys_IsCurrentThreadStopRequested() ) {
			return;
		}

		usleep( 1000 );

		const int previousTicNumber = com_ticNumber;
		common->Async();
		for ( int tic = previousTicNumber; tic < com_ticNumber; ++tic ) {
			Sys_TriggerEvent( TRIGGER_EVENT_ONE );
		}
	}
}

/*
 ==============
 Sys_DefaultSavePath
 ==============
 */
const char *Sys_DefaultSavePath(void) {
	const char *xdgDataHome = getenv( "XDG_DATA_HOME" );
	if ( xdgDataHome && xdgDataHome[0] == '/' ) {
		savepath = xdgDataHome;
		savepath.StripTrailing( '/' );
#if defined( ID_DEMO_BUILD )
		savepath += "/openprey-demo";
#else
		savepath += "/openprey";
#endif
		return savepath.c_str();
	}

	const char *home = getenv( "HOME" );
	if ( home && home[0] ) {
		savepath = home;
		savepath.StripTrailing( '/' );
#if defined( ID_DEMO_BUILD )
		savepath += "/.local/share/openprey-demo";
#else
		savepath += "/.local/share/openprey";
#endif
	} else {
		savepath = Posix_Cwd();
	}
	return savepath.c_str();
}
/*
==============
Sys_EXEPath
==============
*/
const char *Sys_EXEPath( void ) {
	static char	buf[ 4096 ];
	int			len;
	char		linkpath[ 64 ];

	buf[ 0 ] = '\0';
	idStr::snPrintf( linkpath, sizeof( linkpath ), "/proc/%ld/exe", static_cast<long>( getpid() ) );
	len = readlink( linkpath, buf, sizeof( buf ) - 1 );
	if ( len == -1 ) {
		Sys_Printf("couldn't stat exe path link %s: %s\n", linkpath, strerror( errno ));
		buf[ 0 ] = '\0';
		return buf;
	}
	if ( len >= static_cast<int>( sizeof( buf ) - 1 ) ) {
		Sys_Printf( "exe path link %s is too long for the %d byte buffer\n", linkpath, static_cast<int>( sizeof( buf ) ) );
		buf[ 0 ] = '\0';
		return buf;
	}
	buf[ len ] = '\0';
	return buf;
}

/*
==============
Sys_GetPackageRootDirectory
==============
*/
bool Sys_GetPackageRootDirectory( char *packageRoot, int packageRootSize ) {
	// Linux packages stage modules and assets next to the executable; there is
	// no separate application-bundle package root.
	if ( packageRoot != NULL && packageRootSize > 0 ) {
		packageRoot[0] = '\0';
	}
	return false;
}

bool Sys_GetGameModuleRootDirectory( char *moduleRoot, int moduleRootSize ) {
	if ( moduleRoot != NULL && moduleRootSize > 0 ) {
		moduleRoot[0] = '\0';
	}
	return false;
}

/*
================
Sys_DefaultBasePath

Get the default base path
- binary image path
- current directory
- hardcoded
Try to be intelligent: if there is no BASE_GAMEDIR, try the next path
================
*/
const char *Sys_DefaultBasePath(void) {
	struct stat st;
	idStr testbase;
	basepath = Sys_EXEPath();
	if ( basepath.Length() ) {
		basepath.StripFilename();
		testbase = basepath; testbase += "/"; testbase += BASE_GAMEDIR;
		if ( stat( testbase.c_str(), &st ) != -1 && S_ISDIR( st.st_mode ) ) {
			return basepath.c_str();
		} else {
			common->Printf( "no '%s' directory in exe path %s, skipping\n", BASE_GAMEDIR, basepath.c_str() );
		}
	}
	if ( basepath != Posix_Cwd() ) {
		basepath = Posix_Cwd();
		testbase = basepath; testbase += "/"; testbase += BASE_GAMEDIR;
		if ( stat( testbase.c_str(), &st ) != -1 && S_ISDIR( st.st_mode ) ) {
			return basepath.c_str();
		} else {
			common->Printf("no '%s' directory in cwd path %s, skipping\n", BASE_GAMEDIR, basepath.c_str());
		}
	}
	common->Printf( "WARNING: using hardcoded default base path\n" );
	return LINUX_DEFAULT_PATH;
}

/*
===============
Sys_GetConsoleKey
===============
*/
#if !defined(USE_SDL3)
unsigned char Sys_GetConsoleKey( bool shifted ) {
	return shifted ? '~' : '`';
}
#endif

/*
===============
Sys_Shutdown
===============
*/
void Sys_Shutdown( void ) {
	basepath.Clear();
	savepath.Clear();
	Posix_Shutdown();
}

/*
===============
Sys_GetProcessorId
===============
*/
cpuid_t Sys_GetProcessorId( void ) {
	return CPUID_GENERIC;
}

/*
===============
Sys_GetProcessorString
===============
*/
const char *Sys_GetProcessorString( void ) {
	static bool initialized = false;
	static idStr processorString;

	if ( !initialized ) {
		idStr processorName;
		int logicalCount = 0;
		int physicalCount = 0;
		int packageCount = 0;

		Sys_GetLinuxProcessorInfo( processorName, logicalCount, physicalCount, packageCount );
		processorString = Sys_FormatProcessorSummary( processorName.c_str(), CPUSTRING, physicalCount, logicalCount, packageCount, Sys_GetApproximateProcessorFrequencyHz() );
		initialized = true;
	}

	return processorString.c_str();
}

/*
===============
Sys_FPU_EnableExceptions
===============
*/
void Sys_FPU_EnableExceptions( int exceptions ) {
}

/*
===============
Sys_FPE_handler
===============
*/
void Sys_FPE_handler( int signum, siginfo_t *info, void *context ) {
	assert( signum == SIGFPE );
	Sys_Printf( "FPE\n" );
}

/*
===============
Sys_GetClockticks

CLOCK_MONOTONIC nanoseconds. Do not use rdtsc here: the TSC ticks at the
invariant TSC rate, not at the momentary core frequency /proc/cpuinfo
reports, so calibrating one against the other skews every consumer
(FPS counter, com_maxfps throttle, timers).
===============
*/
double Sys_GetClockTicks( void ) {
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double)ts.tv_sec * 1000000000.0 + (double)ts.tv_nsec;
}

/*
===============
Sys_ClockTicksPerSecond
===============
*/
double Sys_ClockTicksPerSecond(void) {
	return 1000000000.0;
}

/*
===============
Sys_GetApproximateProcessorFrequencyHz

Display-only CPU frequency for the processor summary, from the first
"cpu MHz" line of /proc/cpuinfo. Returns 0.0 when unavailable.
===============
*/
double Sys_GetApproximateProcessorFrequencyHz( void ) {
	static bool		init = false;
	static double	ret = 0.0;

	if ( init ) {
		return ret;
	}
	init = true;

	idStr cpuInfoText;
	if ( Sys_ReadLinuxCpuInfo( cpuInfoText ) ) {
		int start = 0;
		while ( start <= cpuInfoText.Length() ) {
			idStr key;
			idStr line;
			idStr value;
			const int lineEnd = cpuInfoText.Find( '\n', start );
			const int lineLength = ( lineEnd >= 0 ) ? lineEnd - start : cpuInfoText.Length() - start;

			line = cpuInfoText.Mid( start, lineLength );
			line.StripTrailing( '\r' );
			line.StripTrailingWhitespace();

			const int delimiter = line.Find( ':' );
			if ( delimiter >= 0 ) {
				key = line.Left( delimiter );
				value = line.Mid( delimiter + 1, line.Length() - delimiter - 1 );
				key.StripTrailingWhitespace();
				value.StripLeading( ' ' );
				value.StripLeading( '\t' );
				value.StripTrailingWhitespace();

				if ( key.Icmp( "cpu MHz" ) == 0 && value.Length() > 0 ) {
					const double cpuMhz = atof( value.c_str() );
					if ( cpuMhz > 0.0 ) {
						ret = cpuMhz * 1000000.0;
						return ret;
					}
				}
			}

			if ( lineEnd < 0 ) {
				break;
			}
			start = lineEnd + 1;
		}
	}

	return ret;
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
	long	count, pageSize;

	count = sysconf( _SC_PHYS_PAGES );
	if ( count <= 0 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PHYS_PAGES failed\n" );
		return 512;
	}	
	pageSize = sysconf( _SC_PAGE_SIZE );
	if ( pageSize <= 0 ) {
		common->Printf( "GetSystemRam: sysconf _SC_PAGE_SIZE failed\n" );
		return 512;
	}
	return Sys_RoundSystemRamMegabytes(
		static_cast<unsigned long long>( count ) * static_cast<unsigned long long>( pageSize ),
		512
	);
}

/*
==================
Sys_DoStartProcess
if we don't fork, this function only returns when exec fails
the no-fork path lets shutdown hand off to an installer or URL helper
==================
*/
void Sys_DoStartProcess( const char *exeName, bool dofork ) {	
	if ( exeName == NULL || exeName[0] == '\0' ) {
		Sys_Printf( "Sys_DoStartProcess: empty command\n" );
		return;
	}

	if ( Sys_IsRegularFile( exeName ) && !Sys_StringHasControlCharacters( exeName ) ) {
		char *argv[2];
		argv[0] = const_cast<char *>( exeName );
		argv[1] = NULL;
		(void)Sys_ExecProcessArgs( argv, dofork );
		return;
	}

	char commandBuffer[MAX_PROCESS_COMMAND];
	char *argv[MAX_PROCESS_ARGS];
	if ( !Sys_ParseProcessCommandLine( exeName, commandBuffer, sizeof( commandBuffer ), argv, MAX_PROCESS_ARGS ) ) {
		Sys_Printf( "Sys_DoStartProcess: invalid command line '%s'\n", exeName );
		return;
	}

	(void)Sys_ExecProcessArgs( argv, dofork );
}

/*
=================
Sys_OpenURL
=================
*/
void idSysLocal::OpenURL( const char *url, bool quit ) {
	const char	*script_path;
	idFile		*script_file;

	static bool	quit_spamguard = false;

	if ( quit_spamguard ) {
		common->DPrintf( "Sys_OpenURL: already in a doexit sequence, ignoring %s\n", url ? url : "" );
		return;
	}

	common->Printf( "Open URL: %s\n", url );
	if ( !Sys_IsSafeURL( url ) ) {
		common->Printf( "OpenURL '%s' rejected: expected a URL with a safe scheme\n", url ? url : "" );
		return;
	}

	// opening an URL on *nix can mean a lot of things .. 
	// prefer a user-provided script, then fall back to freedesktop helpers.

	// look in the savepath first, then in the basepath
	script_path = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_savepath" ), "", "openurl.sh" );
	script_file = fileSystem->OpenExplicitFileRead( script_path );
	if ( !script_file ) {
		script_path = fileSystem->BuildOSPath( cvarSystem->GetCVarString( "fs_basepath" ), "", "openurl.sh" );
		script_file = fileSystem->OpenExplicitFileRead( script_path );
	}
	if ( !script_file ) {
		idStr openerPath;
		if ( Sys_FindExecutableOnPath( "xdg-open", openerPath ) ) {
			char *argv[3];
			argv[0] = const_cast<char *>( openerPath.c_str() );
			argv[1] = const_cast<char *>( url );
			argv[2] = NULL;
			if ( Sys_QueueOrStartProcessArgs( argv, quit ) && quit ) {
				quit_spamguard = true;
			}
			return;
		}
		if ( Sys_FindExecutableOnPath( "gio", openerPath ) ) {
			char *argv[4];
			argv[0] = const_cast<char *>( openerPath.c_str() );
			argv[1] = const_cast<char *>( "open" );
			argv[2] = const_cast<char *>( url );
			argv[3] = NULL;
			if ( Sys_QueueOrStartProcessArgs( argv, quit ) && quit ) {
				quit_spamguard = true;
			}
			return;
		}
		if ( Sys_FindExecutableOnPath( "sensible-browser", openerPath ) ) {
			char *argv[3];
			argv[0] = const_cast<char *>( openerPath.c_str() );
			argv[1] = const_cast<char *>( url );
			argv[2] = NULL;
			if ( Sys_QueueOrStartProcessArgs( argv, quit ) && quit ) {
				quit_spamguard = true;
			}
			return;
		}
		common->Printf( "Can't find URL script 'openurl.sh' or a freedesktop URL opener on PATH\n" );
		common->Printf( "OpenURL '%s' failed\n", url );
		return;
	}
	fileSystem->CloseFile( script_file );

	common->Printf( "URL script: %s\n", script_path );
	char *argv[3];
	argv[0] = const_cast<char *>( script_path );
	argv[1] = const_cast<char *>( url );
	argv[2] = NULL;

	// if we are going to quit, only accept a single URL before quitting and spawning the script
	if ( quit ) {
		quit_spamguard = true;
	}

	(void)Sys_QueueOrStartProcessArgs( argv, quit );
}

/*
 ==================
 Sys_DoPreferences
 ==================
 */
void Sys_DoPreferences( void ) { }

/*
================
Sys_FPU_SetDAZ
================
*/
void Sys_FPU_SetDAZ( bool enable ) {
	/*
	DWORD dwData;

	_asm {
		movzx	ecx, byte ptr enable
		and		ecx, 1
		shl		ecx, 6
		STMXCSR	dword ptr dwData
		mov		eax, dwData
		and		eax, ~(1<<6)	// clear DAX bit
		or		eax, ecx		// set the DAZ bit
		mov		dwData, eax
		LDMXCSR	dword ptr dwData
	}
	*/
}

/*
================
Sys_FPU_SetFTZ
================
*/
void Sys_FPU_SetFTZ( bool enable ) {
	/*
	DWORD dwData;

	_asm {
		movzx	ecx, byte ptr enable
		and		ecx, 1
		shl		ecx, 15
		STMXCSR	dword ptr dwData
		mov		eax, dwData
		and		eax, ~(1<<15)	// clear FTZ bit
		or		eax, ecx		// set the FTZ bit
		mov		dwData, eax
		LDMXCSR	dword ptr dwData
	}
	*/
}

/*
===============
mem consistency stuff
===============
*/

#ifdef ID_MCHECK

const char *mcheckstrings[] = {
	"MCHECK_DISABLED",
	"MCHECK_OK",
	"MCHECK_FREE",	// block freed twice
	"MCHECK_HEAD",	// memory before the block was clobbered
	"MCHECK_TAIL"	// memory after the block was clobbered
};

void abrt_func( mcheck_status status ) {
	Sys_Printf( "memory consistency failure: %s\n", mcheckstrings[ status + 1 ] );
	Posix_SetExit( EXIT_FAILURE );
	common->Quit();
}

#endif

static void Sys_HandlePendingQuitSignal( void ) {
	const int quitSignal = Posix_ConsumeQuitSignal();
	if ( quitSignal == 0 ) {
		return;
	}

	Posix_SetExit( 128 + quitSignal );
	common->Printf( "Exiting on %s\n", Posix_SignalName( quitSignal ) );
	common->Quit();
}

/*
===============
main
===============
*/
int main(int argc, const char **argv) {
#ifdef ID_MCHECK
	// must have -lmcheck linkage
	mcheck( abrt_func );
	Sys_Printf( "memory consistency checking enabled\n" );
#endif
	
	Posix_EarlyInit( );
#ifndef ID_DEDICATED
	Sys_ReportWaylandRuntime();
	Sys_ShowSplash();
#endif

	if ( argc > 1 ) {
		common->Init( argc-1, &argv[1], NULL );
	} else {
		common->Init( 0, NULL, NULL );
	}
#ifndef ID_DEDICATED
	Sys_DestroySplash();
#endif

	Sys_HandlePendingQuitSignal();
	Posix_LateInit( );

	while (1) {
		Sys_HandlePendingQuitSignal();
		common->Frame();
	}
}
