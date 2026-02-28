
/*
===============================================================================

	Definitions for information that is related to a licensee's game name and location.

===============================================================================
*/

// RAVEN BEGIN
// rjohnson: this is the name of the game we are making
#define PROJECT_NAME					"OpenPrey"
#define PROJECT_AUTHOR					"themuffinator"
#define PROJECT_COMPANY				"DarkMatter Productions"
#define PROJECT_VERSION				"0.0.1"
#define PROJECT_WEBSITE				"www.darkmatter-quake.com"
#define PROJECT_REPO					"https://github.com/themuffinator/OpenPrey"

#define GAME_NAME						PROJECT_NAME			// appears on window titles and errors
#define GAME_ICON						"q4icon.bmp"

#define ENGINE_VERSION					PROJECT_NAME "-" PROJECT_VERSION

// jnewquist: build type
#if defined(_DEBUG)
#define GAME_BUILD_TYPE					"Debug"
#elif defined(_MPBETA)
#define GAME_BUILD_TYPE					"MPBeta"
#elif defined(_FINAL)
#define GAME_BUILD_TYPE					""
#elif defined(_RELEASE)
#define	GAME_BUILD_TYPE					""
#endif

// Ensure GAME_BUILD_TYPE is always defined (some build systems only define NDEBUG).
#ifndef GAME_BUILD_TYPE
#define GAME_BUILD_TYPE					""
#endif

// paths
#define	CD_BASEDIR						"OpenPrey"
#define	BASE_GAMEDIR					"base"
#define	BASE_MPGAMEDIR					"base"
#define OPENPREY_GAMEDIR				"openprey"
#define	DEMO_GAMEDIR					"demo"
#define	GAME_PLAYERDEFNAME				"player_tommy"
#define GAME_PLAYERDEFNAME_MP			"player_tommy_mp"

// filenames
#if defined( _M_X64 ) || defined( __x86_64__ )
	#define OPENPREY_BINARY_ARCH		"x64"
#elif defined( _M_IX86 ) || defined( __i386__ )
	#define OPENPREY_BINARY_ARCH		"x86"
#elif defined( _M_ARM64 ) || defined( __aarch64__ )
	#define OPENPREY_BINARY_ARCH		"arm64"
#else
	#define OPENPREY_BINARY_ARCH		"unknown"
#endif
#define	CD_EXE							PROJECT_NAME "-client_" OPENPREY_BINARY_ARCH ".exe"

#ifdef _XENON
#define CONFIG_FILE						"save:/OpenPreyConfig.cfg"
#else
#define CONFIG_FILE						"OpenPreyConfig.cfg"
#endif

// base folder where the source code lives
#define SOURCE_CODE_BASE_FOLDER			"code"

#define DEVELOPER_DOMAIN				"darkmatter-quake.com"
// RAVEN END


// RAVEN BEGIN
// rjohnson: changed the host to our temp address
// default idnet host address
#ifndef IDNET_HOST
	#define IDNET_HOST					"q4master.idsoftware.com"
#endif
// RAVEN END

// default idnet master port
#ifndef IDNET_MASTER_PORT
	#define IDNET_MASTER_PORT			"27650"
#endif

#ifndef MOTD_HOST
	#define MOTD_HOST					"q4m-test.ravensoft.com"
#endif

#ifndef MOTD_PORT
	#define MOTD_PORT					"27700"
#endif

// default network server port
#ifndef PORT_SERVER
//RAVEN BEGIN
#define	PORT_SERVER					28004
//RAVEN END
#endif

// Q4TV default network repeater port
#ifndef PORT_REPEATER
#define PORT_REPEATER					28104
#endif

#ifndef PORT_HTTP
#define PORT_HTTP					28004
#endif

// broadcast scan this many ports after PORT_SERVER so a single machine can run multiple servers
#define	NUM_SERVER_PORTS				4

// see ASYNC_PROTOCOL_VERSION
// use a different major for each game
// RAVEN BEGIN
// ddynerman: rev ASYNC_PROTOCOL_MAJOR to 2 for Quake 4
#define ASYNC_PROTOCOL_MAJOR			2
// RAVEN END

// Savegame Version
// Update when you can no longer maintain compatibility with previous savegames.
// For testing, we're using the build number to ensure no one ever tries to load a stale savegame
#define SAVEGAME_VERSION				1

// editor info
#define EDITOR_WINDOWTEXT				"QuakeEdit"

// win32 info
#define WIN32_CONSOLE_CLASS				"OpenPrey WinConsole"
#define WIN32_SPLASH_CLASS				"OpenPrey Splash"
#define	WIN32_WINDOW_CLASS_NAME			"OpenPrey"
#define	WIN32_FAKE_WINDOW_CLASS_NAME	"OPENPREY_WGL_FAKE"

#ifdef __linux__
	#define DEFAULT_BASE_PATH				"/usr/local/games/openprey"
#elif defined( MACOS_X )
	#define DEFAULT_BASE_PATH				"/Applications/OpenPrey"
#endif

// CD Key file info
#define CDKEY_FILE						"preykey"
#define CDKEY_TEXT						"\n// Do not give this file to ANYONE.\n" \
										"// Human Head Studios and 2K Games will NOT ask you to send this file to them.\n"

// FIXME: Update to Doom
// Product ID. Stored in "productid.txt".
//										This file is copyright 1999 Id Software, and may not be duplicated except during a licensed installation of the full commercial version of Quake 3:Arena
#undef PRODUCT_ID
#define PRODUCT_ID						220, 129, 255, 108, 244, 163, 171, 55, 133, 65, 199, 36, 140, 222, 53, 99, 65, 171, 175, 232, 236, 193, 210, 250, 169, 104, 231, 231, 21, 201, 170, 208, 135, 175, 130, 136, 85, 215, 71, 23, 96, 32, 96, 83, 44, 240, 219, 138, 184, 215, 73, 27, 196, 247, 55, 139, 148, 68, 78, 203, 213, 238, 139, 23, 45, 205, 118, 186, 236, 230, 231, 107, 212, 1, 10, 98, 30, 20, 116, 180, 216, 248, 166, 35, 45, 22, 215, 229, 35, 116, 250, 167, 117, 3, 57, 55, 201, 229, 218, 222, 128, 12, 141, 149, 32, 110, 168, 215, 184, 53, 31, 147, 62, 12, 138, 67, 132, 54, 125, 6, 221, 148, 140, 4, 21, 44, 198, 3, 126, 12, 100, 236, 61, 42, 44, 251, 15, 135, 14, 134, 89, 92, 177, 246, 152, 106, 124, 78, 118, 80, 28, 42
#undef PRODUCT_ID_LENGTH
#define PRODUCT_ID_LENGTH				152

#define CONFIG_SPEC						"config.spec"

// Prey compatibility feature toggles.
#define SINGLE_MAP_BUILD				1
#define PARTICLE_BOUNDS					1
#define SOUND_TOOLS_BUILD				1
#define GUIS_IN_DEMOS					1
#define MUSICAL_LEVELLOADS				1
#define GAMEPORTAL_PVS					1
#define GAMEPORTAL_SOUND				1
#define DEATHWALK_AUTOLOAD				1
#define AUTOMAP							0
#define _HH_RENDERDEMO_HACKS			1
#define _HH_CLIP_FASTSECTORS			1
#define NEW_MESH_TRANSFORM				1
#define SIMD_SHADOW						0
#define MULTICORE						0
#define DEBUG_SOUND_LOG					0
#define _HH_SECUROM						0
#define _HH_INLINED_PROC_CLIPMODELS		0

#ifdef ID_DEDICATED
#define _HH_MYGAMES_SAVES				0
#else
#define _HH_MYGAMES_SAVES				1
#endif

#if 1
#define GOLD							1
#define CONSOLE_IDENTITY				0
#define REMOTE_DMAP						0
#else
#define GOLD							0
#define CONSOLE_IDENTITY				1
#define REMOTE_DMAP						1
#endif

#ifdef _GERMAN_BUILD_
	#define GERMAN_VERSION				1
#else
	#define GERMAN_VERSION				0
#endif
