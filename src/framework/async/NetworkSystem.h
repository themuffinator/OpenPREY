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

#ifndef __NETWORKSYSTEM_H__
#define __NETWORKSYSTEM_H__

#ifndef OPENPREY_ENABLE_BOTS
#define OPENPREY_ENABLE_BOTS 0
#endif

#ifndef OPENPREY_ENABLE_MVD
#define OPENPREY_ENABLE_MVD 0
#endif

#ifndef OPENPREY_ENABLE_REPEATER
#define OPENPREY_ENABLE_REPEATER 0
#endif


/*
===============================================================================

  Network System.

===============================================================================
*/

typedef struct {
	idStr		nickname;
	idStr		clan;
	short		ping;
	int			rate;
} scannedClient_t;

typedef struct {
	netadr_t	adr;
	idDict		serverInfo;
	int			ping;

	int			clients;

	int			OSMask;

	//RAVEN BEGIN
	// shouchard:  added favorite flag
	bool		favorite;	// true if this has been marked by a user as a favorite
	bool		dedicated;
	// shouchard:  added performance filtered flag
	bool		performanceFiltered;	// true if the client machine is too wimpy to have good performance
//RAVEN END
} scannedServer_t;

typedef enum {
	SC_NONE = -1,
	SC_FAVORITE,
	SC_LOCKED,
	SC_DEDICATED,
	SC_PB,
	SC_NAME,
	SC_PING,
	SC_REPEATER,
	SC_PLAYERS,
	SC_GAMETYPE,
	SC_MAP,
	SC_ALL,
	NUM_SC
} sortColumn_t;

typedef struct {
	sortColumn_t			column;
	idList<int>::cmp_t* compareFn;
	idList<int>::filter_t* filterFn;
	const char* description;
} sortInfo_t;

class idNetworkSystem {
public:
	virtual					~idNetworkSystem( void ) {}

	// Keep this virtual prefix byte-for-byte compatible with Prey's v7 game API.
	virtual void			ServerSendReliableMessage( int clientNum, const idBitMsg &msg );
	virtual void			ServerSendReliableMessageExcluding( int clientNum, const idBitMsg &msg );
	virtual int				ServerGetClientPing( int clientNum );
	virtual int				ServerGetClientPrediction( int clientNum );
	virtual int				ServerGetClientTimeSinceLastPacket( int clientNum );
	virtual int				ServerGetClientTimeSinceLastInput( int clientNum );
	virtual int				ServerGetClientOutgoingRate( int clientNum );
	virtual int				ServerGetClientIncomingRate( int clientNum );
	virtual float			ServerGetClientIncomingPacketLoss( int clientNum );

	virtual void			ClientSendReliableMessage( const idBitMsg &msg );
	virtual int				ClientGetPrediction( void );
	virtual int				ClientGetTimeSinceLastPacket( void );
	virtual int				ClientGetOutgoingRate( void );
	virtual int				ClientGetIncomingRate( void );
	virtual float			ClientGetIncomingPacketLoss( void );

	// Engine-only helpers below are deliberately non-virtual. Adding them to the
	// vtable would shift every Prey idNetworkSystem call across the DLL boundary.
	const char *			GetServerAddress( void );
	void					SetLoadingText( const char *loadingText );
	void					AddLoadingIcon( const char *icon );

#if OPENPREY_ENABLE_MVD
	// OPENPREY-GATED(D9): upstream MVD routing is outside game API v7.
	void					ServerSendReliableMessageNoDemo( int clientNum, const idBitMsg &msg );
	void					ServerSendReliableMessageExcludingNoDemo( int clientNum, const idBitMsg &msg );
	void					ServerRecordInstanceReliableMessage( int instance, int excludeClient, const idBitMsg &msg );
#endif

#if OPENPREY_ENABLE_BOTS
	// OPENPREY-GATED(D9): upstream bot entry points are outside game API v7.
	int					AllocateClientSlotForBot( const char *botName, int maxPlayersOnServer );
	int					ServerSetBotUserCommand( int clientNum, int frameNum, const usercmd_t &cmd );
	int					ServerSetBotUserName( int clientNum, const char *playerName );
	int					ServerConnectBot( void ) { return -1; }
#endif

	// RAVEN BEGIN
// ddynerman: added some utility functions
	// uses a static buffer, copy it before calling in game again
	const char* GetClientAddress(int clientNum) { return 0; }
	void			AddFriend(int clientNum) { }
	void			RemoveFriend(int clientNum) { }
	// for MP games
	const char* GetClientGUID(int clientNum) { return 0; }
	// RAVEN END

	void			GetTrafficStats(int& bytesSent, int& packetsSent, int& bytesReceived, int& packetsReceived) const { }

	// server browser
	int				GetNumScannedServers(void) { return 0; }
	const scannedServer_t* GetScannedServerInfo(int serverNum) { return 0; }
	const scannedClient_t* GetScannedServerClientInfo(int serverNum, int clientNum) { return 0; }
	void			AddSortFunction(const sortInfo_t& sortInfo) { }
	bool			RemoveSortFunction(const sortInfo_t& sortInfo) { return 0; }
	void			UseSortFunction(const sortInfo_t& sortInfo, bool use = true) { }
	bool			SortFunctionIsActive(const sortInfo_t& sortInfo) { return 0; }

	// returns true if enabled
	bool			HTTPEnable(bool enable) { return 0; }

	void			ClientSetServerInfo(const idDict& serverSI) { }

#if OPENPREY_ENABLE_REPEATER
	// OPENPREY-GATED(D9): repeater support is not exposed through game API v7.
	void			RepeaterSetInfo(const idDict& info) { }
	const char* GetViewerGUID(int clientNum) { return 0; }
	int				RepeaterGetClientNum(int clientId) { return -1; }
#endif

	int				ServerGetClientNum(int clientId) { return 0; }
	int				ServerGetServerTime(void) { return 0; }
};

extern idNetworkSystem *	networkSystem;

#endif /* !__NETWORKSYSTEM_H__ */
