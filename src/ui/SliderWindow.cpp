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




#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"
#include "SliderWindow.h"

static const float SLIDER_SCROLLBAR_THUMB_RANGE_DIVISOR = 15.0f;

static float SliderVolumeToDB( float value ) {
	return value <= 0.0f ? -60.0f : ( value >= 1.0f ? 0.0f : idMath::Log( value ) * ( 6.0f / 0.693147181f ) );
}

static float SliderDBToVolume( float db ) {
	return db <= -60.0f ? 0.0f : ( db >= 0.0f ? 1.0f : idMath::Pow( 2.0f, db * ( 1.0f / 6.0f ) ) );
}

/*
============
idSliderWindow::CommonInit
============
*/
void idSliderWindow::CommonInit() {
	value = 0.0;
	low = 0.0;
	high = 100.0;
	stepSize = 1.0;
	thumbMat = declManager->FindMaterial("_default");
	buddyWin = NULL;

	cvar = NULL;
	cvar_init = false;
	liveUpdate = true;

	vertical = false;
	scrollbar = false;

	verticalFlip = false;
	volumeSlider = false;
	lastValue = 0.0f;
}

idSliderWindow::idSliderWindow(idDeviceContext *d, idUserInterfaceLocal *g) : idWindow(d, g) {
	dc = d;
	gui = g;
	CommonInit();
}

idSliderWindow::idSliderWindow(idUserInterfaceLocal *g) : idWindow(g) {
	gui = g;
	CommonInit();
}

idSliderWindow::~idSliderWindow() {

}

bool idSliderWindow::ParseInternalVar(const char *_name, idParser *src) {
	if (idStr::Icmp(_name, "stepsize") == 0 || idStr::Icmp(_name, "step") == 0) {
		stepSize = src->ParseFloat();
		return true;
	}
	if (idStr::Icmp(_name, "low") == 0) {
		low = src->ParseFloat();
		return true;
	}
	if (idStr::Icmp(_name, "high") == 0) {
		high = src->ParseFloat();
		return true;
	}
	if (idStr::Icmp(_name, "vertical") == 0) {
		vertical = src->ParseBool();
		return true;
	}
	if (idStr::Icmp(_name, "verticalflip") == 0) {
		verticalFlip = src->ParseBool();
		return true;
	}
	if (idStr::Icmp(_name, "volumeslider") == 0) {
		volumeSlider = src->ParseBool();
		return true;
	}
	if (idStr::Icmp(_name, "scrollbar") == 0) {
		scrollbar = src->ParseBool();
		return true;
	}
	if (idStr::Icmp(_name, "thumbshader") == 0) {
		ParseString(src, thumbShader);
		declManager->FindMaterial(thumbShader);
		return true;
	}
	return idWindow::ParseInternalVar(_name, src);
}

idWinVar *idSliderWindow::GetWinVarByName(const char *_name, bool fixup, drawWin_t** owner) {
 
	if (idStr::Icmp(_name, "value") == 0) {
		return &value;
	}
	if (idStr::Icmp(_name, "cvar") == 0) {
		return &cvarStr;
	}
	if ( idStr::Icmp( _name, "liveUpdate" ) == 0 ) {
		return &liveUpdate;
	}
	if ( idStr::Icmp( _name, "cvarGroup" ) == 0 ) {
		return &cvarGroup;
	}
	
	return idWindow::GetWinVarByName(_name, fixup, owner);
}

const char *idSliderWindow::HandleEvent(const sysEvent_t *event, bool *updateVisuals) {
	cmd = "";

	if (!(event->evType == SE_KEY && event->evValue2)) {
		return "";
	}

	int key = event->evValue;

	if ( event->evValue2 && key == K_MOUSE1 ) {
		SetCapture(this);
		return RouteMouseCoords(0.0f, 0.0f);
	} 

	UpdateCvar( true );
	value = ClampAndSnapValue( value );

	bool handled = true;
	const float verticalUpStep = verticalFlip ? stepSize : -stepSize;
	const float verticalDownStep = verticalFlip ? -stepSize : stepSize;
	switch ( key ) {
		case K_MWHEELUP:
			if ( scrollbar ) {
				value = value + ( vertical ? verticalUpStep : -stepSize );
			} else {
				handled = false;
			}
			break;
		case K_MWHEELDOWN:
			if ( scrollbar ) {
				value = value + ( vertical ? verticalDownStep : stepSize );
			} else {
				handled = false;
			}
			break;
		case K_RIGHTARROW:
		case K_KP_RIGHTARROW:
			value = value + stepSize;
			break;
		case K_LEFTARROW:
		case K_KP_LEFTARROW:
			value = value - stepSize;
			break;
		case K_UPARROW:
		case K_KP_UPARROW:
			value = value + ( vertical ? verticalUpStep : -stepSize );
			break;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
			value = value + ( vertical ? verticalDownStep : stepSize );
			break;
		case K_PGUP:
		case K_KP_PGUP:
			value = value + ( vertical ? ( verticalFlip ? GetPageStep() : -GetPageStep() ) : -GetPageStep() );
			break;
		case K_PGDN:
		case K_KP_PGDN:
			value = value + ( vertical ? ( verticalFlip ? -GetPageStep() : GetPageStep() ) : GetPageStep() );
			break;
		case K_HOME:
		case K_KP_HOME:
			value = low;
			break;
		case K_END:
		case K_KP_END:
			value = high;
			break;
		case K_MOUSE2: {
			const float pageStep = GetPageStep();
			if ( vertical ) {
				const bool belowThumb = gui->CursorY() > thumbRect.y + thumbRect.h * 0.5f;
				value = value + ( belowThumb ? ( verticalFlip ? -pageStep : pageStep ) : ( verticalFlip ? pageStep : -pageStep ) );
			} else {
				const bool rightOfThumb = gui->CursorX() > thumbRect.x + thumbRect.w * 0.5f;
				value = value + ( rightOfThumb ? pageStep : -pageStep );
			}
			break;
		}
		default:
			handled = false;
			break;
	}

	if ( handled ) {
		CommitValue();
		return cmd;
	}

	return "";
}


void idSliderWindow::SetBuddy(idWindow *buddy) {
	buddyWin = buddy;
}

void idSliderWindow::PostParse() {
	idWindow::PostParse();
	value = 0.0;
	thumbMat = declManager->FindMaterial(thumbShader);
	thumbMat->SetSort( SS_GUI );
	thumbWidth = thumbMat->GetImageWidth();
	thumbHeight = thumbMat->GetImageHeight();
	//vertical = state.GetBool("vertical");
	//scrollbar = state.GetBool("scrollbar");
	flags |= (WIN_HOLDCAPTURE | WIN_CANFOCUS);
	InitCvar();
}

void idSliderWindow::InitWithDefaults(const char *_name, const idRectangle &_rect, const idVec4 &_foreColor, const idVec4 &_matColor, const char *_background, const char *thumbShader, bool _vertical, bool _scrollbar) {
	SetInitialState(_name);
	rect = _rect;
	foreColor = _foreColor;
	matColor = _matColor;
	thumbMat = declManager->FindMaterial(thumbShader);
	thumbMat->SetSort( SS_GUI );
	thumbWidth = thumbMat->GetImageWidth();
	thumbHeight = thumbMat->GetImageHeight();
	background = declManager->FindMaterial(_background);
	background->SetSort( SS_GUI );
	vertical = _vertical;
	scrollbar = _scrollbar;
	flags |= WIN_HOLDCAPTURE;
}

void idSliderWindow::SetRange(float _low, float _high, float _step) {
	low = _low;
	high = _high;
	stepSize = _step;
	value = ClampAndSnapValue( value );
}

void idSliderWindow::SetValue(float _value) {
	value = ClampAndSnapValue( _value );
}

void idSliderWindow::UpdateThumbMetrics( const idRectangle& sliderRect ) {
	if ( thumbMat == NULL ) {
		thumbWidth = thumbHeight = 0.0f;
		return;
	}
	const float baseWidth = Max( 0.0f, static_cast<float>( thumbMat->GetImageWidth() ) );
	const float baseHeight = Max( 0.0f, static_cast<float>( thumbMat->GetImageHeight() ) );
	thumbWidth = baseWidth;
	thumbHeight = baseHeight;
	if ( !scrollbar ) {
		return;
	}
	const float axisLength = vertical ? sliderRect.h : sliderRect.w;
	if ( axisLength <= 0.0f ) {
		if ( vertical ) { thumbHeight = 0.0f; } else { thumbWidth = 0.0f; }
		return;
	}
	const float baseSize = idMath::ClampFloat( 0.0f, axisLength, vertical ? baseHeight : baseWidth );
	const float sizingRange = axisLength / SLIDER_SCROLLBAR_THUMB_RANGE_DIVISOR;
	const float range = idMath::ClampFloat( 0.0f, sizingRange, Max( high - low, 0.0f ) );
	const float thumbSize = sizingRange > 0.0f ?
		idMath::ClampFloat( baseSize, axisLength, baseSize + ( axisLength - baseSize ) * ( 1.0f - range / sizingRange ) ) : baseSize;
	if ( vertical ) { thumbHeight = thumbSize; } else { thumbWidth = thumbSize; }
}

void idSliderWindow::Draw(int time, float x, float y) {
	idVec4 color = foreColor;

	if ( !cvar && !buddyWin ) {
		return;
	}

	UpdateThumbMetrics( drawRect );

	UpdateCvar( true );
	value = ClampAndSnapValue( value );

	float range = high - low;

	if ( range <= 0.0f ) {
		return;
	}

	float thumbPos = (range) ? (value - low) / range : 0.0;
	if (vertical) {
		if ( verticalFlip ) {
			thumbPos = 1.f - thumbPos;
		}
		thumbPos *= drawRect.h - thumbHeight;
		thumbPos += drawRect.y;
		thumbRect.y = thumbPos;
		thumbRect.x = drawRect.x;
	} else {
		thumbPos *= drawRect.w - thumbWidth;
		thumbPos += drawRect.x;
		thumbRect.x = thumbPos;
		thumbRect.y = drawRect.y;
	}
	thumbRect.w = thumbWidth;
	thumbRect.h = thumbHeight;

	if ( hover && !noEvents && Contains(gui->CursorX(), gui->CursorY()) ) {
		color = hoverColor;
	} else {
		hover = false;
	}
	if ( flags & WIN_CAPTURE ) {
		color = hoverColor;
		hover = true;
	}

	dc->DrawMaterial(thumbRect.x, thumbRect.y, thumbRect.w, thumbRect.h, thumbMat, color);
	if ( !scrollbar && ( flags & WIN_FOCUS ) ) {
		dc->DrawRect(thumbRect.x+1.0f, thumbRect.y+1.0f, thumbRect.w-2.0f, thumbRect.h-2.0f, 1.0f, color);
	}
}


void idSliderWindow::DrawBackground(const idRectangle &_drawRect) {
	if ( !cvar && !buddyWin ) {
		return;
	}

	if ( high - low <= 0.0f ) {
		return;
	}
	UpdateThumbMetrics( _drawRect );

	idRectangle r = _drawRect;
	if (!scrollbar) {
		if ( vertical ) {
			r.y += thumbHeight / 2.f;
			r.h -= thumbHeight;
		} else {
			r.x += thumbWidth / 2.0;
			r.w -= thumbWidth;
		}
	}
	idWindow::DrawBackground(r);
}

const char *idSliderWindow::RouteMouseCoords(float xd, float yd) {
	cmd = "";

	if (!(flags & WIN_CAPTURE)) {
		return "";
	}

	if ( high <= low ) {
		value = low;
		CommitValue();
		return cmd;
	}

	float pct;
	idRectangle r = drawRect;
	r.x = actualX;
	r.y = actualY;
	UpdateThumbMetrics( r );

	if (vertical) {
		r.y += thumbHeight / 2;
		r.h -= thumbHeight;
		if ( r.h <= 0.0f ) {
			value = low;
		} else if (gui->CursorY() >= r.y && gui->CursorY() <= r.Bottom()) {
			pct = (gui->CursorY() - r.y) / r.h;
			if ( verticalFlip ) {
				pct = 1.f - pct;
			}
			value = low + (high - low) * pct;
		} else if (gui->CursorY() < r.y) {
			if ( verticalFlip ) {
				value = high;
			} else {
				value = low;
			}
		} else {
			if ( verticalFlip ) {
				value = low;
			} else {
				value = high;
			}
		}
	} else {
		r.x += thumbWidth / 2;
		r.w -= thumbWidth;
		if ( r.w <= 0.0f ) {
			value = low;
		} else if (gui->CursorX() >= r.x && gui->CursorX() <= r.Right()) {
			pct = (gui->CursorX() - r.x) / r.w;
			value = low + (high - low) * pct;
		} else if (gui->CursorX() < r.x) {
			value = low;
		} else {
			value = high;
		}
	}

	CommitValue();

	return cmd;
}

float idSliderWindow::ClampAndSnapValue( float newValue ) const {
	if ( high <= low ) {
		return low;
	}

	newValue = idMath::ClampFloat( low, high, newValue );
	if ( stepSize > 0.0f ) {
		const float steps = idMath::Floor( ( ( newValue - low ) / stepSize ) + 0.5f );
		newValue = low + steps * stepSize;
		newValue = idMath::ClampFloat( low, high, newValue );
	}
	return newValue;
}

float idSliderWindow::GetPageStep( void ) const {
	const float range = high - low;
	if ( range <= 0.0f ) {
		return 0.0f;
	}

	const float step = stepSize > 0.0f ? stepSize : 1.0f;
	const float pageStep = range * 0.25f;
	return Max( step, pageStep );
}

void idSliderWindow::CommitValue( void ) {
	value = ClampAndSnapValue( value );
	const bool changed = idMath::Fabs( static_cast<float>( value ) - lastValue ) > 0.0001f;
	if ( buddyWin ) {
		buddyWin->HandleBuddyUpdate( this );
	} else {
		gui->SetStateFloat( cvarStr, value );
		UpdateCvar( false );
		if ( scrollbar ) {
			RunScript( ON_ACTION );
		}
	}
	lastValue = value;
	if ( changed ) {
		RunScript( ON_SLIDERCHANGE );
	}
}

void idSliderWindow::MouseEnter() {
	idWindow::MouseEnter();

	if ( !scrollbar || noEvents || gui == NULL ) {
		return;
	}

	idWindow *desktop = gui->GetDesktop();
	if ( desktop == NULL || desktop->GetCaptureChild() != NULL ) {
		return;
	}

	idWindow *focusTarget = ( buddyWin != NULL ) ? buddyWin : this;
	if ( desktop->GetFocusedChild() != focusTarget ) {
		desktop->SetFocus( focusTarget, false );
	}
}


void idSliderWindow::Activate(bool activate, idStr &act) {
	idWindow::Activate(activate, act);
	if ( activate ) {
		UpdateCvar( true, true );
	}
}

/*
============
idSliderWindow::InitCvar
============
*/
void idSliderWindow::InitCvar( ) {
	if ( cvarStr[0] == '\0' ) {
		if ( !buddyWin ) {
			common->Warning( "idSliderWindow::InitCvar: gui '%s' window '%s' has an empty cvar string", gui->GetSourceFile(), name.c_str() );
		}
		cvar_init = true;
		cvar = NULL;
		return;
	}

	cvar = cvarSystem->Find( cvarStr );
	if ( !cvar ) {
		common->Warning( "idSliderWindow::InitCvar: gui '%s' window '%s' references undefined cvar '%s'", gui->GetSourceFile(), name.c_str(), cvarStr.c_str() );
		cvar_init = true;
		return;
	}
}

/*
============
idSliderWindow::UpdateCvar
============
*/
void idSliderWindow::UpdateCvar( bool read, bool force ) {
	if ( buddyWin || !cvar ) {
		return;
	}
	if ( force || liveUpdate ) {
		if ( read ) {
			value = ClampAndSnapValue( volumeSlider ? SliderDBToVolume( cvar->GetFloat() ) : cvar->GetFloat() );
			lastValue = value;
			if ( force || idMath::Fabs( static_cast<float>( value ) - gui->State().GetFloat( cvarStr ) ) > 0.0001f ) {
				gui->SetStateFloat( cvarStr, value );
			}
		} else {
			value = ClampAndSnapValue( gui->State().GetFloat( cvarStr ) );
			gui->SetStateFloat( cvarStr, value );
			const float cvarValue = volumeSlider ? SliderVolumeToDB( value ) : static_cast<float>( value );
			if ( force || idMath::Fabs( cvarValue - cvar->GetFloat() ) > 0.0001f ) {
				cvar->SetFloat( cvarValue );
			}
		}
	}
}

/*
============
idSliderWindow::RunNamedEvent
============
*/
void idSliderWindow::RunNamedEvent( const char* eventName ) {
	idStr event, group;
	
	if ( !idStr::Cmpn( eventName, "cvar read ", 10 ) ) {
		event = eventName;
		group = event.Mid( 10, event.Length() - 10 );
		if ( !group.Cmp( cvarGroup ) ) {
			UpdateCvar( true, true );
		}
	} else if ( !idStr::Cmpn( eventName, "cvar write ", 11 ) ) {
		event = eventName;
		group = event.Mid( 11, event.Length() - 11 );
		if ( !group.Cmp( cvarGroup ) ) {
			UpdateCvar( false, true );
		}
	}
}

