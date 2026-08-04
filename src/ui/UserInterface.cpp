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




#include "ListGUILocal.h"
#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"

extern idCVar r_skipGuiShaders;		// 1 = don't render any gui elements on surfaces
extern idCVar gui_debugScript;
idCVar ui_aspectCorrection( "ui_aspectCorrection", "1", CVAR_GUI | CVAR_ARCHIVE | CVAR_BOOL,
	"preserve classic 4:3 layout for 2D UI (menu, HUD, console, loading/init); 0 = stretch to full 2D viewport" );

idUserInterfaceManagerLocal	uiManagerLocal;
idUserInterfaceManager *	uiManager = &uiManagerLocal;

namespace {

static void SetStateRectangleComponents( idUserInterfaceLocal *gui, const char *prefix, const idRectangle &rect ) {
	if ( gui == NULL || prefix == NULL ) {
		return;
	}

	gui->SetStateFloat( va( "%s_x", prefix ), rect.x );
	gui->SetStateFloat( va( "%s_y", prefix ), rect.y );
	gui->SetStateFloat( va( "%s_w", prefix ), rect.w );
	gui->SetStateFloat( va( "%s_h", prefix ), rect.h );
}

}

/*
===============================================================================

	idUserInterfaceManagerLocal

===============================================================================
*/

void idUserInterfaceManagerLocal::Init() {
	screenRect = idRectangle(0, 0, 640, 480);
	dc.Init();
}

void idUserInterfaceManagerLocal::Shutdown() {
	guis.DeleteContents( true );
	alwaysThinkGUIs.Clear();
	demoGuis.DeleteContents( true );
	dc.Shutdown();
}

void idUserInterfaceManagerLocal::Touch( const char *name ) {
	idUserInterface *gui = Alloc();
	gui->InitFromFile( name );
//	delete gui;
}

void idUserInterfaceManagerLocal::WritePrecacheCommands( idFile *f ) {

	int c = guis.Num();
	for( int i = 0; i < c; i++ ) {
		idStr command = "touchGui ";
		command += guis[i]->Name();
		command += "\n";
		common->Printf( "%s", command.c_str() );
		f->Printf( "%s", command.c_str() );
	}
}

void idUserInterfaceManagerLocal::SetSize( float width, float height ) {
	if ( width > 0.0f && height > 0.0f ) {
		screenRect = idRectangle( 0.0f, 0.0f, width, height );
	}
	dc.SetSize( width, height );
}

void idUserInterfaceManagerLocal::SetAspectCorrection( bool enabled ) {
	dc.SetAspectCorrection( enabled );
}

void idUserInterfaceManagerLocal::BeginLevelLoad() {
	int c = guis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( (guis[ i ]->GetDesktop()->GetFlags() & WIN_MENUGUI) == 0 ) {
			guis[ i ]->ClearRefs();
			/*
			delete guis[ i ];
			guis.RemoveIndex( i );
			i--; c--;
			*/
		}
	}
}

void idUserInterfaceManagerLocal::EndLevelLoad() {
	int c = guis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( guis[i]->GetRefs() == 0 ) {
			//common->Printf( "purging %s.\n", guis[i]->GetSourceFile() );

			// use this to make sure no materials still reference this gui
			bool remove = true;
			for ( int j = 0; j < declManager->GetNumDecls( DECL_MATERIAL ); j++ ) {
				const idMaterial *material = static_cast<const idMaterial *>(declManager->DeclByIndex( DECL_MATERIAL, j, false ));
				if ( material->GlobalGui() == guis[i] ) {
					remove = false;
					break;
				}
			}
			if ( remove ) {
				RemoveAlwaysThinkGui( guis[i] );
				delete guis[ i ];
				guis.RemoveIndex( i );
				i--; c--;
			}
		}
	}

	// icons registered before their image was resident can be sized now
	dc.SizeIcons();
}

// RAVEN BEGIN
// bdube: embedded icons
// The game registers the inline text icons it needs ( weapon and means-of-death
// obituary icons, team/ready/voice icons ) from "icon <code>" spawn args while
// caching entity def media, so this has to reach the device context or every
// ^i escape in game text silently draws nothing.
void idUserInterfaceManagerLocal::RegisterIcon( const char *code, const char *shader, int x, int y, int w, int h ) {
	dc.RegisterIcon( code, shader, x, y, w, h );
}
// RAVEN END

void idUserInterfaceManagerLocal::Reload( bool all ) {
	ID_TIME_T ts;

	int c = guis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( !all ) {
			fileSystem->ReadFile( guis[i]->GetSourceFile(), NULL, &ts );
			if ( ts <= guis[i]->GetTimeStamp() ) {
				continue;
			}
		}

		guis[i]->InitFromFile( guis[i]->GetSourceFile() );
		common->Printf( "reloading %s.\n", guis[i]->GetSourceFile() );
	}
}

void idUserInterfaceManagerLocal::ListGuis() const {
	int c = guis.Num();
	common->Printf( "\n   size   refs   name\n" );
	size_t total = 0;
	int copies = 0;
	int unique = 0;
	for ( int i = 0; i < c; i++ ) {
		idUserInterfaceLocal *gui = guis[i];
		size_t sz = gui->Size();
		bool isUnique = guis[i]->interactive;
		if ( isUnique ) {
			unique++;
		} else {
			copies++;
		}
		common->Printf( "%6.1fk %4i (%s) %s ( %i transitions )\n", sz / 1024.0f, guis[i]->GetRefs(), isUnique ? "unique" : "copy", guis[i]->GetSourceFile(), guis[i]->desktop->NumTransitions() );
		total += sz;
	}
	common->Printf( "===========\n  %i total Guis ( %i copies, %i unique ), %.2f total Mbytes", c, copies, unique, total / ( 1024.0f * 1024.0f ) );
}

bool idUserInterfaceManagerLocal::CheckGui( const char *qpath ) const {
	idFile *file = fileSystem->OpenFileRead( qpath );
	if ( file ) {
		fileSystem->CloseFile( file );
		return true;
	}
	return false;
}

idUserInterface *idUserInterfaceManagerLocal::Alloc( void ) const {
	return new idUserInterfaceLocal();
}

void idUserInterfaceManagerLocal::DeAlloc( idUserInterface *gui ) {
	if ( gui ) {
		int c = guis.Num();
		for ( int i = 0; i < c; i++ ) {
			if ( guis[i] == gui ) {
				RemoveAlwaysThinkGui( guis[i] );
				delete guis[i];
				guis.RemoveIndex( i );
				return;
			}
		}
	}
}

idUserInterface *idUserInterfaceManagerLocal::FindGui( const char *qpath, bool autoLoad, bool needUnique, bool forceNOTUnique ) {
	int c = guis.Num();

	for ( int i = 0; i < c; i++ ) {
		idUserInterfaceLocal *gui = guis[i];
		if ( !idStr::Icmp( guis[i]->GetSourceFile(), qpath ) ) {
			// Retail keeps unique GUI instances isolated even when state changes make them temporarily noninteractive.
			if ( !forceNOTUnique && ( needUnique || guis[i]->IsInteractive() || guis[i]->IsUniqued() ) ) {
				break;
			}
			guis[i]->AddRef();
			return guis[i];
		}
	}

	if ( autoLoad ) {
		idUserInterface *gui = Alloc();
		if ( gui->InitFromFile( qpath ) ) {
			gui->SetUniqued( forceNOTUnique ? false : needUnique );
			return gui;
		} else {
			delete gui;
		}
	}
	return NULL;
}

idUserInterface *idUserInterfaceManagerLocal::FindDemoGui( const char *qpath ) {
	int c = demoGuis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( !idStr::Icmp( demoGuis[i]->GetSourceFile(), qpath ) ) {
			return demoGuis[i];
		}
	}
	return NULL;
}

idListGUI *	idUserInterfaceManagerLocal::AllocListGUI( void ) const {
	return new idListGUILocal();
}

void idUserInterfaceManagerLocal::FreeListGUI( idListGUI *listgui ) {
	delete listgui;
}

void idUserInterfaceManagerLocal::UpdateAlwaysThinkGui( idUserInterfaceLocal *gui ) {
	if ( gui == NULL || gui->desktop == NULL || !gui->desktop->AlwaysThink() ) {
		RemoveAlwaysThinkGui( gui );
		return;
	}

	alwaysThinkGUIs.AddUnique( gui );
}

void idUserInterfaceManagerLocal::RemoveAlwaysThinkGui( idUserInterfaceLocal *gui ) {
	if ( gui == NULL ) {
		return;
	}

	alwaysThinkGUIs.Remove( gui );
}

void idUserInterfaceManagerLocal::RunAlwaysThinkGUIs( int time ) {
	for ( int i = 0; i < alwaysThinkGUIs.Num(); i++ ) {
		idUserInterfaceLocal *gui = alwaysThinkGUIs[i];
		if ( gui == NULL || guis.Find( gui ) == NULL || gui->desktop == NULL || !gui->desktop->AlwaysThink() ) {
			alwaysThinkGUIs.RemoveIndex( i );
			i--;
			continue;
		}

		gui->time = time;
		gui->desktop->RunTimeEvents( time );
	}
}

/*
===============================================================================

	idUserInterfaceLocal

===============================================================================
*/

idUserInterfaceLocal::idUserInterfaceLocal() {
	cursorX = cursorY = 0.0;
	desktop = NULL;
	loading = false;
	active = false;
	interactive = false;
	uniqued = false;
	initialized = false;
	bindHandler = NULL;
	lightColorVar = NULL;
	//so the reg eval in gui parsing doesn't get bogus values
	time = 0;
	refs = 1;
}

idUserInterfaceLocal::~idUserInterfaceLocal() {
	delete desktop;
	desktop = NULL;
}

const char *idUserInterfaceLocal::Name() const {
	return source;
}

const char *idUserInterfaceLocal::Comment() const {
	if ( desktop ) {
		return desktop->GetComment();
	}
	return "";
}

bool idUserInterfaceLocal::IsInteractive() const {
	return interactive;
}

void idUserInterfaceLocal::SetInteractive(bool interactive) {
	this->interactive = interactive;
}

bool idUserInterfaceLocal::InitFromFile( const char *qpath, bool rebuild, bool cache ) { 

	if ( !( qpath && *qpath ) ) { 
		// FIXME: Memory leak!!
		return false;
	}

	int sz = sizeof( idWindow );
	sz = sizeof( idSimpleWindow );
	loading = true;

	if ( rebuild ) {
		delete desktop;
		desktop = new idWindow( this );
	} else if ( desktop == NULL ) {
		desktop = new idWindow( this );
	}

	source = qpath;
	state.Set( "text", "Test Text!" );

	idParser src( LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT | LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT );

	//Load the timestamp so reload guis will work correctly
	fileSystem->ReadFile(qpath, NULL, &timeStamp);

	src.LoadFile( qpath );

	if ( src.IsLoaded() ) {
		idToken token;
		while( src.ReadToken( &token ) ) {
			if ( idStr::Icmp( token, "windowDef" ) == 0 ) {
				desktop->SetDC( &uiManagerLocal.dc );
				desktop->SetWindowDefType( token.c_str() );
				if ( desktop->Parse( &src, rebuild ) ) {
					desktop->SetFlag( WIN_DESKTOP );
					desktop->FixupParms();
				}
				continue;
			}
			else if ( idStr::Icmp( token, "animationDef" ) == 0 ||
					  idStr::Icmp( token, "buttonDef" ) == 0 ||
					  idStr::Icmp( token, "superWindowDef" ) == 0 ||
					  idStr::Icmp( token, "tabContainerDef" ) == 0 ||
					  idStr::Icmp( token, "tabDef" ) == 0 ||
					  idStr::Icmp( token, "keyDef" ) == 0 ||
					  idStr::Icmp( token, "creditDef" ) == 0 ||
					  idStr::Icmp( token, "splineDef" ) == 0 ||
					  idStr::Icmp( token, "bindDef" ) == 0 ||
					  idStr::Icmp( token, "bindKeyDef" ) == 0 ) {
				const idStr childDefType = token;
				idToken childName;
				desktop->SetDC( &uiManagerLocal.dc );
				if ( !src.ExpectTokenType( TT_NAME, 0, &childName ) ) {
					continue;
				}
				src.UnreadToken( &childName );
				idWindow *child = new idWindow( &uiManagerLocal.dc, this );
				child->SetWindowDefType( childDefType.c_str() );
				if ( child->Parse( &src, rebuild ) ) {
					desktop->SetFlag( WIN_DESKTOP );
					desktop->AddChildWindow( child );
					child->FixupParms();
				} else {
					delete child;
				}
				continue;
			}
			else {
				common->Error("Parsing gui %s invalid token %s\n", qpath, token.c_str());
			}
		}

		state.Set( "name", qpath );
	} else {
		desktop->SetDC( &uiManagerLocal.dc );
		desktop->SetFlag( WIN_DESKTOP );
		desktop->name = "Desktop";
		desktop->text = va( "Invalid GUI: %s", qpath );
		desktop->rect = idRectangle( 0.0f, 0.0f, 640.0f, 480.0f );
		desktop->drawRect = desktop->rect;
		desktop->foreColor = idVec4( 1.0f, 1.0f, 1.0f, 1.0f );
		desktop->backColor = idVec4( 0.0f, 0.0f, 0.0f, 1.0f );
		desktop->SetupFromState();
		common->Warning( "Couldn't load gui: '%s'", qpath );
	}

	interactive = desktop->Interactive();

	if ( uiManagerLocal.guis.Find( this ) == NULL ) {
		uiManagerLocal.guis.Append( this );
	}
	uiManagerLocal.UpdateAlwaysThinkGui( this );

	loading = false;
	lightColorVar = NULL;
	initialized = false;

	return true; 
}

const char *idUserInterfaceLocal::HandleEvent( const sysEvent_t *event, int _time, bool *updateVisuals ) {

	time = _time;

	if ( bindHandler && event->evType == SE_KEY && event->evValue2 == 1 ) {
		const char *ret = bindHandler->HandleEvent( event, updateVisuals );
		bindHandler = NULL;
		return ret;
	}

	if ( event->evType == SE_MOUSE ) {
		cursorX += event->evValue;
		cursorY += event->evValue2;

		// Retail clamps the cursor to the virtual screen, and the game's
		// in-world gui interaction depends on it: idPlayer::UpdateFocus parks
		// the cursor with a large negative move (expecting it to stop at the
		// corner) before every absolute reposition, so an unclamped cursor
		// drifts thousands of units off-canvas and clicks never hit a window.
		// Menu guis may extend past the 4:3 canvas when aspect correction is
		// active, so honor the expanded bounds there instead of trapping the
		// cursor at the canvas edge.
		float minX = 0.0f;
		float minY = 0.0f;
		float maxX = static_cast<float>( VIRTUAL_WIDTH );
		float maxY = static_cast<float>( VIRTUAL_HEIGHT );
		if ( desktop != NULL ) {
			maxX = desktop->forceAspectWidth;
			maxY = desktop->forceAspectHeight;
			if ( ( desktop->GetFlags() & WIN_MENUGUI ) && ui_aspectCorrection.GetBool() ) {
				float xExpand = 0.0f;
				float yExpand = 0.0f;
				uiManagerLocal.dc.GetVirtualScreenExpansion( maxX, maxY, xExpand, yExpand );
				minX -= xExpand;
				maxX += xExpand;
				minY -= yExpand;
				maxY += yExpand;
			}
		}
		cursorX = idMath::ClampFloat( minX, maxX, cursorX );
		cursorY = idMath::ClampFloat( minY, maxY, cursorY );
	}

	if ( desktop ) {
		return desktop->HandleEvent( event, updateVisuals );
	} 

	return "";
}

void idUserInterfaceLocal::HandleNamedEvent ( const char* eventName ) {
	desktop->RunNamedEvent( eventName );
}

void idUserInterfaceLocal::Redraw( int _time, bool useAspectCorrection ) {
	SetStateInt( "mousex", (int)CursorX() );
	SetStateInt( "mousey", (int)CursorY() );
	if ( r_skipGuiShaders.GetInteger() > 5 ) {
		return;
	}
	if ( !loading && desktop ) {
		time = _time;
		renderSystem->SetFrameShaderTime( _time );
		if ( !initialized ) {
			initialized = true;
			desktop->Init();
		}

		const bool aspectCorrect = useAspectCorrection && ui_aspectCorrection.GetBool();
		uiManagerLocal.SetAspectCorrection( aspectCorrect );
		uiManagerLocal.SetSize( desktop->forceAspectWidth, desktop->forceAspectHeight );
		float xExpand = 0.0f;
		float yExpand = 0.0f;
		uiManagerLocal.dc.GetVirtualScreenExpansion( desktop->forceAspectWidth, desktop->forceAspectHeight, xExpand, yExpand );
		SetStateFloat( "virtual_screen_x_expand", xExpand );
		SetStateFloat( "virtual_screen_y_expand", yExpand );

		idRectangle cinematicTopBar;
		idRectangle cinematicBottomBar;
		idRectangle cinematicLeftBar;
		idRectangle cinematicRightBar;
		idRectangle cinematicVisibleArea;
		uiManagerLocal.dc.GetCinematic16x9Bars( desktop->forceAspectWidth, desktop->forceAspectHeight, cinematicTopBar, cinematicBottomBar, cinematicLeftBar, cinematicRightBar, cinematicVisibleArea );
		SetStateRectangleComponents( this, "cinematic_bar_top", cinematicTopBar );
		SetStateRectangleComponents( this, "cinematic_bar_bottom", cinematicBottomBar );
		SetStateRectangleComponents( this, "cinematic_bar_left", cinematicLeftBar );
		SetStateRectangleComponents( this, "cinematic_bar_right", cinematicRightBar );
		SetStateRectangleComponents( this, "cinematic_visible_area", cinematicVisibleArea );

		if ( gui_debugScript.GetInteger() > 4 ) {
			static int lastDebugRedrawTime = -10000;
			if ( _time - lastDebugRedrawTime >= 250 ) {
				common->Printf( "GUI: redraw gui=%s time=%d active=%d interactive=%d visible=%d r_skipGuiShaders=%d cursor=%.1f,%.1f\n",
					source.c_str(),
					_time,
					active ? 1 : 0,
					interactive ? 1 : 0,
					desktop->visible ? 1 : 0,
					r_skipGuiShaders.GetInteger(),
					cursorX, cursorY );
				lastDebugRedrawTime = _time;
			}
		}
		uiManagerLocal.dc.PushClipRect( uiManagerLocal.screenRect );
		desktop->Redraw( 0, 0 );
		uiManagerLocal.dc.PopClipRect();
	}
}

void idUserInterfaceLocal::DrawCursor() {
	if ( !desktop || desktop->GetFlags() & WIN_MENUGUI ) {
		uiManagerLocal.dc.DrawCursor(&cursorX, &cursorY, 15.0f );
	} else {
		uiManagerLocal.dc.DrawCursor(&cursorX, &cursorY, 64.0f );
	}
}

const idDict &idUserInterfaceLocal::State() const {
	return state;
}

void idUserInterfaceLocal::DeleteStateVar( const char *varName ) {
	state.Delete( varName );
}

void idUserInterfaceLocal::SetStateString( const char *varName, const char *value ) {
	const char *oldValue = state.GetString( varName, "" );
	state.Set( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		const char *newValue = value ? value : "";
		if ( idStr::Icmp( oldValue, newValue ) != 0 ) {
			common->Printf( "GUI: state %s = \"%s\" (was \"%s\") gui=%s\n", varName, newValue, oldValue, source.c_str() );
		}
	}
}

void idUserInterfaceLocal::SetStateBool( const char *varName, const bool value ) {
	bool oldValue = state.GetBool( varName, "0" );
	state.SetBool( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		if ( oldValue != value ) {
			common->Printf( "GUI: state %s = %d (was %d) gui=%s\n", varName, value ? 1 : 0, oldValue ? 1 : 0, source.c_str() );
		}
	}
}

void idUserInterfaceLocal::SetStateInt( const char *varName, const int value ) {
	int oldValue = state.GetInt( varName, "0" );
	state.SetInt( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		if ( oldValue != value ) {
			common->Printf( "GUI: state %s = %d (was %d) gui=%s\n", varName, value, oldValue, source.c_str() );
		}
	}
}

void idUserInterfaceLocal::SetStateFloat( const char *varName, const float value ) {
	float oldValue = state.GetFloat( varName, "0" );
	state.SetFloat( varName, value );
	if ( gui_debugScript.GetInteger() > 3 ) {
		if ( oldValue != value ) {
			common->Printf( "GUI: state %s = %.4f (was %.4f) gui=%s\n", varName, value, oldValue, source.c_str() );
		}
	}
}

const char* idUserInterfaceLocal::GetStateString( const char *varName, const char* defaultString ) const {
	return state.GetString(varName, defaultString);
}

bool idUserInterfaceLocal::GetStateBool( const char *varName, const char* defaultString ) const {
	return state.GetBool(varName, defaultString); 
}

int idUserInterfaceLocal::GetStateInt( const char *varName, const char* defaultString ) const {
	return state.GetInt(varName, defaultString);
}

float idUserInterfaceLocal::GetStateFloat( const char *varName, const char* defaultString ) const {
	return state.GetFloat(varName, defaultString);
}

idVec4 idUserInterfaceLocal::GetLightColor( void ) {
	if ( lightColorVar ) {
		return *lightColorVar;
	}
	return vec4_origin;
}

void idUserInterfaceLocal::StateChanged( int _time, bool redraw ) {
	time = _time;
	if (desktop) {
		desktop->StateChanged( redraw );
	}
	if ( state.GetBool( "noninteractive" ) ) {
		interactive = false;
	}
	else {
		if (desktop) {
			interactive = desktop->Interactive();
		} else {
			interactive = false;
		}
	}
}

const char *idUserInterfaceLocal::Activate(bool activate, int _time) {
	time = _time;
	active = activate;
	if ( desktop ) {
		activateStr = "";
		desktop->Activate( activate, activateStr );
		return activateStr;
	}
	return "";
}

void idUserInterfaceLocal::Trigger(int _time) {
	time = _time;
	if ( desktop ) {
		desktop->Trigger();
	}
}

void idUserInterfaceLocal::CallStartup() {
	if ( desktop ) {
		desktop->RunScript( idWindow::ON_STARTUP );
		desktop->RunNamedEvent( "onStartup" );
	}
}

const char* idUserInterfaceLocal::Translate( const char* text ) {
	return common->GetLanguageDict()->GetString( text != NULL ? text : "" );
}

void idUserInterfaceLocal::ReadFromDemoFile( class idDemoFile *f ) {
	idStr work;
	f->ReadDict( state );
	source = state.GetString("name");

	if (desktop == NULL) {
		f->Log("creating new gui\n");
		desktop = new idWindow(this);
	   	desktop->SetFlag( WIN_DESKTOP );
	   	desktop->SetDC( &uiManagerLocal.dc );
		desktop->ReadFromDemoFile(f);
	} else {
		f->Log("re-using gui\n");
		desktop->ReadFromDemoFile(f, false);
	}

	f->ReadFloat( cursorX );
	f->ReadFloat( cursorY );

	bool add = true;
	int c = uiManagerLocal.demoGuis.Num();
	for ( int i = 0; i < c; i++ ) {
		if ( uiManagerLocal.demoGuis[i] == this ) {
			add = false;
			break;
		}
	}

	if (add) {
		uiManagerLocal.demoGuis.Append(this);
	}
}

void idUserInterfaceLocal::WriteToDemoFile( class idDemoFile *f ) {
	idStr work;
	f->WriteDict( state );
	if (desktop) {
		desktop->WriteToDemoFile(f);
	}

	f->WriteFloat( cursorX );
	f->WriteFloat( cursorY );
}

static const int OPENPREY_GUI_SAVE_MAGIC = 'P' | ( 'G' << 8 ) | ( 'U' << 16 ) | ( 'I' << 24 );
static const int OPENPREY_GUI_SAVE_VERSION = 1;

bool idUserInterfaceLocal::WriteToSaveGame( idFile *savefile ) const {
	int len;
	const idKeyValue *kv;
	const char *string;

	savefile->Write( &OPENPREY_GUI_SAVE_MAGIC, sizeof( OPENPREY_GUI_SAVE_MAGIC ) );
	savefile->Write( &OPENPREY_GUI_SAVE_VERSION, sizeof( OPENPREY_GUI_SAVE_VERSION ) );

	int num = state.GetNumKeyVals();
	savefile->Write( &num, sizeof( num ) );

	for( int i = 0; i < num; i++ ) {
		kv = state.GetKeyVal( i );
		len = kv->GetKey().Length();
		string = kv->GetKey().c_str();
		savefile->Write( &len, sizeof( len ) );
		savefile->Write( string, len );

		len = kv->GetValue().Length();
		string = kv->GetValue().c_str();
		savefile->Write( &len, sizeof( len ) );
		savefile->Write( string, len );
	}

	savefile->Write( &active, sizeof( active ) );
	savefile->Write( &interactive, sizeof( interactive ) );
	savefile->Write( &uniqued, sizeof( uniqued ) );
	savefile->Write( &time, sizeof( time ) );
	len = activateStr.Length();
	savefile->Write( &len, sizeof( len ) );
	savefile->Write( activateStr.c_str(), len );
	len = pendingCmd.Length();
	savefile->Write( &len, sizeof( len ) );
	savefile->Write( pendingCmd.c_str(), len );
	len = returnCmd.Length();
	savefile->Write( &len, sizeof( len ) );
	savefile->Write( returnCmd.c_str(), len );

	savefile->Write( &cursorX, sizeof( cursorX ) );
	savefile->Write( &cursorY, sizeof( cursorY ) );

	desktop->WriteToSaveGame( savefile );

	return true;
}

static bool UI_ReadSaveGameBytes( idFile *savefile, void *buffer, int len, const char *detail ) {
	const int offset = savefile->Tell();
	const int bytesRead = savefile->Read( buffer, len );
	if ( bytesRead != len ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: truncated %s at offset %d (read %d of %d)",
			detail ? detail : "data", offset, bytesRead, len );
		return false;
	}
	return true;
}

static bool UI_ReadSaveGameString( idFile *savefile, idStr &string, const char *detail ) {
	int len = 0;
	const int offset = savefile->Tell();
	if ( !UI_ReadSaveGameBytes( savefile, &len, sizeof( len ), detail ) ) {
		string.Clear();
		return false;
	}

	const int remainingBytes = Max( 0, savefile->Length() - savefile->Tell() );
	const int maxSavedStringLength = 64 * 1024;
	if ( len < 0 || len > maxSavedStringLength || len > remainingBytes ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: invalid %s length %d at offset %d (remaining %d)",
			detail ? detail : "string", len, offset, remainingBytes );
		string.Clear();
		return false;
	}

	string.Fill( ' ', len );
	if ( len > 0 && !UI_ReadSaveGameBytes( savefile, &string[0], len, detail ) ) {
		string.Clear();
		return false;
	}
	return true;
}

bool idUserInterfaceLocal::ReadFromSaveGame( idFile *savefile ) {
	int magic;
	int version;
	int num;
	int i;
	idStr key;
	idStr value;

	if ( !UI_ReadSaveGameBytes( savefile, &magic, sizeof( magic ), "GUI save magic" ) ||
		!UI_ReadSaveGameBytes( savefile, &version, sizeof( version ), "GUI save version" ) ) {
		return false;
	}
	if ( magic != OPENPREY_GUI_SAVE_MAGIC || version != OPENPREY_GUI_SAVE_VERSION ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: unsupported GUI stream marker 0x%08x version %d",
			magic, version );
		return false;
	}

	if ( !UI_ReadSaveGameBytes( savefile, &num, sizeof( num ), "state count" ) ) {
		return false;
	}
	if ( num < 0 || num > 16384 ) {
		common->Warning( "idUserInterfaceLocal::ReadFromSaveGame: invalid state count %d", num );
		return false;
	}

	state.Clear();
	for( i = 0; i < num; i++ ) {
		if ( !UI_ReadSaveGameString( savefile, key, "state key" ) ) {
			return false;
		}
		if ( !UI_ReadSaveGameString( savefile, value, "state value" ) ) {
			return false;
		}
		
		state.Set( key, value );
	}

	if ( !UI_ReadSaveGameBytes( savefile, &active, sizeof( active ), "active flag" ) ||
		!UI_ReadSaveGameBytes( savefile, &interactive, sizeof( interactive ), "interactive flag" ) ||
		!UI_ReadSaveGameBytes( savefile, &uniqued, sizeof( uniqued ), "unique flag" ) ||
		!UI_ReadSaveGameBytes( savefile, &time, sizeof( time ), "time" ) ) {
		return false;
	}

	if ( !UI_ReadSaveGameString( savefile, activateStr, "activate command" ) ||
		!UI_ReadSaveGameString( savefile, pendingCmd, "pending command" ) ||
		!UI_ReadSaveGameString( savefile, returnCmd, "return command" ) ) {
		return false;
	}

	if ( !UI_ReadSaveGameBytes( savefile, &cursorX, sizeof( cursorX ), "cursor x" ) ||
		!UI_ReadSaveGameBytes( savefile, &cursorY, sizeof( cursorY ), "cursor y" ) ) {
		return false;
	}

	desktop->ReadFromSaveGame( savefile );

	return true;
}

size_t idUserInterfaceLocal::Size() {
	size_t sz = sizeof(*this) + state.Size() + source.Allocated();
	if ( desktop ) {
		sz += desktop->Size();
	}
	return sz;
}

void idUserInterfaceLocal::RecurseSetKeyBindingNames( idWindow *window ) {
	int i;
	idWinVar *v = window->GetWinVarByName( "bind" );
	if ( v ) {
		SetStateString( v->GetName(), idKeyInput::KeysFromBinding( v->GetName() ) );
	}
	i = 0;
	while ( i < window->GetChildCount() ) {
		idWindow *next = window->GetChild( i );
		if ( next ) {
			RecurseSetKeyBindingNames( next );
		}
		i++;
	}
}

/*
==============
idUserInterfaceLocal::SetKeyBindingNames
==============
*/
void idUserInterfaceLocal::SetKeyBindingNames( void ) {
	if ( !desktop ) {
		return;
	}
	// walk the windows
	RecurseSetKeyBindingNames( desktop );
}

/*
==============
idUserInterfaceLocal::SetCursor
==============
*/
void idUserInterfaceLocal::SetCursor( float x, float y ) {
	cursorX = x;
	cursorY = y;
}

bool idUserInterfaceLocal::GetMaxTextIndex( const char *windowName, const char *text, wrapInfo_t& wrapInfo ) const {
	if ( desktop == NULL ) {
		return false;
	}

	drawWin_t *drawWindow = desktop->FindChildByName( windowName );
	if ( drawWindow == NULL ) {
		return false;
	}

	idDeviceContext *measureDc = NULL;
	int pixelLimit = 0;
	float textScale = 0.0f;

	if ( drawWindow->win != NULL ) {
		drawWindow->win->SetFont();
		measureDc = drawWindow->win->dc;
		pixelLimit = static_cast<int>( drawWindow->win->textRect.w );
		textScale = drawWindow->win->textScale;
	} else if ( drawWindow->simp != NULL ) {
		drawWindow->simp->dc->SetFont( drawWindow->simp->fontNum );
		measureDc = drawWindow->simp->dc;
		pixelLimit = static_cast<int>( drawWindow->simp->textRect.w );
		textScale = drawWindow->simp->textScale;
	}

	if ( measureDc == NULL ) {
		return false;
	}

	return measureDc->GetMaxTextIndex( text, pixelLimit, textScale, wrapInfo );
}
