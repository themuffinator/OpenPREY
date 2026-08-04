/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2012-2016 Robert Beckebans
Copyright (C) 2014-2016 Kot in Action Creative Artel

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "snd_local.h"

idCVar s_singleEmitter( "s_singleEmitter", "0", CVAR_INTEGER, "mute all sounds but this emitter" );
idCVar s_showStartSound( "s_showStartSound", "0", CVAR_BOOL, "print a message every time a sound starts/stops" );
idCVar s_skipStartSound( "s_skipStartSound", "0", CVAR_BOOL, "skip starting sounds" );
idCVar s_useOcclusion( "s_useOcclusion", "1", CVAR_BOOL, "Attenuate sounds based on walls" );
idCVar s_centerFractionVO( "s_centerFractionVO", "0.75", CVAR_FLOAT, "Portion of VO sounds routed to the center channel" );

extern idCVar s_playDefaultSound;
extern idCVar s_noSound;
extern idCVar s_volume;
extern idCVar s_musicVolume;
extern idCVar s_constantAmplitude;
extern idCVar s_speakerFraction;
extern idCVar s_radioChatterFraction;
extern idCVar s_quadraticFalloff;
extern idCVar s_frequencyShift;
extern idCVar com_profanity;

static const int SOUND_SHADER_SHAKE_RATE_HZ = 30;
static const float SOUND_SHADER_MATERIAL_SHAKE_SCALE = 2800.0f;
static const float SOUND_SHADER_SHAKE_NORMALIZE = 1.0f / 32768.0f;
static const float SOUND_WORLD_VOLUME_EPSILON = 1.0f / 1024.0f;
static const int Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_0 = 6;
static const int Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_1 = 7;
static const int Q4_SOUND_CHANNEL_RADIO_CHATTER = 10;
static const float SOUND_FREQUENCY_SHIFT_MIN = 0.25f;
static const float SOUND_FREQUENCY_SHIFT_MAX = 4.0f;
static const float SOUND_OCCLUSION_PER_BLOCKED_PORTAL = 0.5f;
static const float SOUND_ENVIROSUIT_OCCLUSION = 0.5f;
static const int SOUND_FADE_MAX_MSEC = 24 * 60 * 60 * 1000;
static const float SOUND_FADE_MAX_DB = 24.0f;
static const int SOUND_UNMUTABLE_PRIORITY_BOOST = 100000;
static const int SOUND_CHANNEL_COMPLETION_GRACE_MSEC = 100;

static ID_INLINE float SoundSanitizeUnitValue( const float value, const float fallback )
{
	if( FLOAT_IS_NAN( value ) )
	{
		return fallback;
	}
	return idMath::ClampFloat( 0.0f, 1.0f, value );
}

static ID_INLINE float SoundSanitizePositiveValue( const float value, const float fallback )
{
	if( FLOAT_IS_NAN( value ) || value <= 0.0f )
	{
		return fallback;
	}
	return value;
}

static ID_INLINE float SoundSanitizeGainScale( const float value, const float fallback )
{
	return idMath::ClampFloat( 0.0f, 16.0f, SoundSanitizePositiveValue( value, fallback ) );
}

static ID_INLINE float SoundSanitizeFadeScale( const float value, const float fallback )
{
	if( FLOAT_IS_NAN( value ) )
	{
		return fallback;
	}
	return idMath::ClampFloat( 0.0f, 16.0f, value );
}

static ID_INLINE int SoundSanitizeFadeMsec( const int length )
{
	return idMath::ClampInt( 0, SOUND_FADE_MAX_MSEC, length );
}

static ID_INLINE int SoundFadeSecondsToMsec( const float seconds )
{
	if( FLOAT_IS_NAN( seconds ) || seconds <= 0.0f )
	{
		return 0;
	}
	const float clampedSeconds = Min( seconds, MS2SEC( SOUND_FADE_MAX_MSEC ) );
	return SEC2MS( clampedSeconds );
}

static ID_INLINE float SoundFadeDBToScale( const float db )
{
	if( FLOAT_IS_NAN( db ) )
	{
		return 0.0f;
	}
	return SoundSanitizeFadeScale( idMath::dBToScale( Min( db, SOUND_FADE_MAX_DB ) ), 0.0f );
}

static ID_INLINE float VolumeScaleToDB( const float volumeScale )
{
	if( volumeScale <= 0.0f )
	{
		return DB_SILENCE;
	}
	return LinearToDB( volumeScale );
}

static ID_INLINE float SoundChannelFrequencyShift( const idSoundChannel* chan )
{
	if( !s_frequencyShift.GetBool() )
	{
		return 1.0f;
	}

	const float frequencyShift = SoundSanitizePositiveValue( chan->parms.frequencyShift, 1.0f );
	return idMath::ClampFloat( SOUND_FREQUENCY_SHIFT_MIN, SOUND_FREQUENCY_SHIFT_MAX, frequencyShift );
}

static float SoundChannelPlaybackTimeMsec( idSoundChannel* chan, const int currentTime )
{
	if( chan == NULL || currentTime <= chan->startTime )
	{
		return 0.0f;
	}

	// Retail keeps a shifted sample clock per live channel instead of deriving every offset from wall time.
	const float frequencyShift = SoundChannelFrequencyShift( chan );
	if( chan->lastFrequencyShiftTime != 0 || frequencyShift != 1.0f )
	{
		float elapsed = 0.0f;
		if( chan->lastFrequencyShiftTime != 0 )
		{
			elapsed = chan->elapsedFrequencyShiftTime + Max( 0, currentTime - chan->lastFrequencyShiftTime ) * chan->lastFrequencyShift;
		}
		else
		{
			elapsed = Max( 0, currentTime - chan->startTime ) * frequencyShift;
		}

		chan->lastFrequencyShift = frequencyShift;
		chan->elapsedFrequencyShiftTime = Max( 0.0f, elapsed );
		chan->lastFrequencyShiftTime = currentTime;
		return chan->elapsedFrequencyShiftTime;
	}

	return currentTime - chan->startTime;
}

static bool SoundChannelHasCompleted( idSoundChannel* chan, const int currentTime )
{
	if( chan == NULL || chan->leadinSample == NULL )
	{
		return true;
	}
	if( chan->endTime == 1 )
	{
		return true;
	}
	if( currentTime < chan->startTime )
	{
		return false;
	}
	if( chan->IsLooping() )
	{
		return false;
	}

	const int length = chan->leadinSample->LengthInMsec();
	if( length <= 0 )
	{
		return chan->endTime > 0 && chan->endTime < currentTime;
	}
	if( chan->lastFrequencyShiftTime == 0 )
	{
		return chan->endTime > 0 && chan->endTime < currentTime;
	}

	return SoundChannelPlaybackTimeMsec( chan, currentTime ) > length + SOUND_CHANNEL_COMPLETION_GRACE_MSEC;
}

static void SoundWorldApplyDistanceFalloff( float& volumeScale, const float distance, const float minDistance, const float maxDistance )
{
	if( FLOAT_IS_NAN( volumeScale ) )
	{
		volumeScale = 0.0f;
		return;
	}
	if( FLOAT_IS_NAN( distance ) || FLOAT_IS_NAN( minDistance ) || FLOAT_IS_NAN( maxDistance ) || maxDistance <= 0.0f )
	{
		return;
	}

	if( distance >= maxDistance )
	{
		volumeScale = 0.0f;
	}
	else if( ( distance > minDistance ) && ( maxDistance > minDistance ) )
	{
		float frac = 1.0f - ( distance - minDistance ) / ( maxDistance - minDistance );
		frac = idMath::ClampFloat( 0.0f, 1.0f, frac );
		if( s_quadraticFalloff.GetBool() )
		{
			frac *= frac;
		}
		volumeScale *= frac;
	}
}

static ID_INLINE float SoundCombineOcclusion( const float a, const float b )
{
	const float clampedA = SoundSanitizeUnitValue( a, 0.0f );
	const float clampedB = SoundSanitizeUnitValue( b, 0.0f );
	return 1.0f - ( ( 1.0f - clampedA ) * ( 1.0f - clampedB ) );
}

static bool IsRadioChatterChannel( const idSoundChannel* chan )
{
	return chan->logicalChannel == Q4_SOUND_CHANNEL_RADIO_CHATTER;
}

static bool IsVoicePlaybackChannel( const idSoundChannel* chan )
{
	return IsRadioChatterChannel( chan ) && ( chan->parms.soundShaderFlags & SSF_GLOBAL ) != 0;
}

static bool SoundParmsAreVoicePlayback( const soundShaderParms_t& parms, const int logicalChannel )
{
	const int voiceFlags = SSF_VO | SSF_IS_VO | SSF_VO_FOR_PLAYER;
	return ( parms.soundShaderFlags & voiceFlags ) != 0 ||
		   ( logicalChannel == Q4_SOUND_CHANNEL_RADIO_CHATTER && ( parms.soundShaderFlags & SSF_GLOBAL ) != 0 );
}

static int SoundPriorityFromVolumeDB( float volumeDB )
{
	if( FLOAT_IS_NAN( volumeDB ) )
	{
		volumeDB = DB_SILENCE;
	}
	volumeDB = idMath::ClampFloat( DB_SILENCE, 24.0f, volumeDB );
	return idMath::Ftoi( volumeDB * 100.0f );
}

static bool ReceivesQuake4ExtraAttenuation( const idSoundChannel* chan )
{
	return chan->logicalChannel != Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_0 &&
		   chan->logicalChannel != Q4_SOUND_CHANNEL_SPECIAL_ATTENUATION_EXEMPT_1 &&
		   chan->parms.soundClass != SOUND_CLASS_MUSICAL;
}

static bool SoundUsesMusicVolume( const soundShaderParms_t& parms )
{
	return ( parms.soundShaderFlags & SSF_MUSIC ) != 0 || parms.soundClass == SOUND_CLASS_MUSICAL;
}

static bool IsProfanityCensorEnabled()
{
	return !com_profanity.GetBool();
}

static int SoundSampleChoiceFromDiversity( const int numEntries, const float diversity )
{
	if( numEntries <= 0 || FLOAT_IS_NAN( diversity ) )
	{
		return 0;
	}

	const int choice = static_cast<int>( diversity * numEntries );
	if( choice < 0 || choice >= numEntries )
	{
		return 0;
	}
	return choice;
}

static int SoundLoopStartOffsetFromDiversity( const int lengthMsec, const float diversity )
{
	if( lengthMsec <= 0 || FLOAT_IS_NAN( diversity ) )
	{
		return 0;
	}

	const int offset = static_cast<int>( lengthMsec * diversity );
	return Max( 0, offset );
}

/*
================================================================================================

	idSoundFade

================================================================================================
*/

/*
========================
idSoundFade::Clear
========================
*/
void idSoundFade::Clear()
{
	fadeStartTime = 0;
	fadeEndTime = 0;
	fadeStartVolume = 1.0f;
	fadeEndVolume = 1.0f;
	fadeHold = false;
}

/*
========================
idSoundFade::SetVolume
========================
*/
void idSoundFade::SetVolume( float to )
{
	to = SoundSanitizeFadeScale( to, 1.0f );
	fadeStartVolume = to;
	fadeEndVolume = to;
	fadeStartTime = 0;
	fadeEndTime = 0;
	fadeHold = false;
}

/*
========================
idSoundFade::Fade
========================
*/
void idSoundFade::Fade( float to, int length, int soundTime )
{
	to = SoundSanitizeFadeScale( to, 0.0f );
	length = SoundSanitizeFadeMsec( length );
	int startTime = soundTime;
	// if it is already fading to this volume at this rate, don't change it
	if( !fadeHold && fadeEndTime == startTime + length && fadeEndVolume == to )
	{
		return;
	}
	fadeStartVolume = GetVolume( soundTime );
	fadeEndVolume = to;
	fadeStartTime = startTime;
	fadeEndTime = startTime + length;
	fadeHold = false;
}

/*
========================
idSoundFade::FadeFrom

Apply a temporary hold in the linear volume pipeline.
========================
*/
void idSoundFade::FadeFrom( float to, int delay, int length, int soundTime )
{
	to = SoundSanitizeFadeScale( to, 0.0f );
	delay = SoundSanitizeFadeMsec( delay );
	length = SoundSanitizeFadeMsec( length );
	const int startTime = soundTime + delay;
	const int endTime = startTime + length;
	if( fadeHold && fadeStartTime == startTime && fadeEndTime == endTime && fadeEndVolume == to )
	{
		return;
	}
	fadeStartVolume = GetVolume( soundTime );
	fadeEndVolume = to;
	fadeStartTime = startTime;
	fadeEndTime = endTime;
	fadeHold = true;
}

/*
========================
idSoundFade::FadeDB
========================
*/
void idSoundFade::FadeDB( float toDB, float overSeconds, int soundTime )
{
	Fade( SoundFadeDBToScale( toDB ), SoundFadeSecondsToMsec( overSeconds ), soundTime );
}

/*
========================
idSoundFade::Sanitize
========================
*/
void idSoundFade::Sanitize()
{
	fadeStartVolume = SoundSanitizeFadeScale( fadeStartVolume, 1.0f );
	fadeEndVolume = SoundSanitizeFadeScale( fadeEndVolume, fadeStartVolume );
	if( fadeEndTime < fadeStartTime )
	{
		fadeEndTime = fadeStartTime;
	}
}

/*
========================
idSoundFade::GetVolume
========================
*/
float idSoundFade::GetVolume( const int soundTime ) const
{
	if( fadeHold )
	{
		return ( soundTime >= fadeStartTime && soundTime <= fadeEndTime ) ? fadeEndVolume : fadeStartVolume;
	}
	const float fadeDuration = ( fadeEndTime - fadeStartTime );
	const int currentTime = soundTime;
	const float playTime = ( currentTime - fadeStartTime );
	const float startVolume = SoundSanitizeFadeScale( fadeStartVolume, 1.0f );
	const float endVolume = SoundSanitizeFadeScale( fadeEndVolume, startVolume );
	if( fadeDuration <= 0.0f )
	{
		return endVolume;
	}
	else if( currentTime >= fadeEndTime )
	{
		return endVolume;
	}
	else if( currentTime > fadeStartTime )
	{
		return startVolume + ( endVolume - startVolume ) * playTime / fadeDuration;
	}
	else
	{
		return startVolume;
	}
}

/*
========================
idSoundChannel::idSoundChannel
========================
*/
idSoundChannel::idSoundChannel()
{
	emitter = NULL;
	hardwareVoice = NULL;

	startTime = 0;
	endTime = 0;
	lastFrequencyShiftTime = 0;
	leadinSample = NULL;
	loopingSample = NULL;
	logicalChannel = SCHANNEL_ANY;
	choice = 0;
	allowSlow = false;
	soundShader = NULL;

	volumeFade.Clear();

	volumeDB = DB_SILENCE;
	currentAmplitude = 0.0f;
	lastFrequencyShift = 1.0f;
	elapsedFrequencyShiftTime = 0.0f;
}

/*
========================
idSoundChannel::~idSoundChannel
========================
*/
idSoundChannel::~idSoundChannel()
{
}

/*
========================
idSoundChannel::CanMute
Never actually mute VO because we can't restart them precisely enough for lip syncing to not fuck up
========================
*/
bool idSoundChannel::CanMute() const
{
	return !IsVoicePlayback();
}

/*
========================
idSoundChannel::IsVoicePlayback
========================
*/
bool idSoundChannel::IsVoicePlayback() const
{
	return SoundParmsAreVoicePlayback( parms, logicalChannel ) || IsVoicePlaybackChannel( this );
}

/*
========================
idSoundChannel::MixPrioritySortKey
========================
*/
int idSoundChannel::MixPrioritySortKey( const int currentTime ) const
{
	(void)currentTime;
	return SoundPriorityFromVolumeDB( volumeDB ) + ( CanMute() ? 0 : SOUND_UNMUTABLE_PRIORITY_BOOST );
}

/*
========================
idSoundChannel::Mute

A muted sound is considered still running, and can restart when a listener
gets close enough.
========================
*/
void idSoundChannel::Mute()
{
	if( hardwareVoice != NULL )
	{
		soundSystemLocal.FreeVoice( hardwareVoice );
		hardwareVoice = NULL;
	}
}

/*
========================
idSoundChannel::IsLooping
========================
*/
bool idSoundChannel::IsLooping() const
{
	return ( parms.soundShaderFlags & SSF_LOOPING ) != 0;
}

/*
========================
idSoundChannel::CheckForCompletion
========================
*/
bool idSoundChannel::CheckForCompletion( int currentTime )
{
	return SoundChannelHasCompleted( this, currentTime );
}

/*
========================
idSoundChannel::UpdateVolume
========================
*/
void idSoundChannel::UpdateVolume( int currentTime )
{
	idSoundWorldLocal* soundWorld = emitter->soundWorld;

	volumeDB = DB_SILENCE;
	currentAmplitude = 0.0f;

	if( leadinSample == NULL )
	{
		return;
	}
	if( startTime > currentTime )
	{
		return;
	}
	if( SoundChannelHasCompleted( this, currentTime ) )
	{
		return;
	}

	// if you don't want to hear all the beeps from missing sounds
	if( leadinSample->IsDefault() && !s_playDefaultSound.GetBool() )
	{
		return;
	}

	bool emitterIsListener = ( emitter->emitterId == soundWorld->listener.id );

	// if it is a private sound, set the volume to zero unless we match the listener.id
	if( parms.soundShaderFlags & SSF_PRIVATE_SOUND )
	{
		if( !emitterIsListener )
		{
			return;
		}
	}
	if( parms.soundShaderFlags & SSF_ANTI_PRIVATE_SOUND )
	{
		if( emitterIsListener )
		{
			return;
		}
	}

	float volumeScale = parms.volume;
	if( soundShader != NULL && leadinSample != NULL && soundShader->leadinVolume != 1.0f )
	{
		const int leadinLength = leadinSample->LengthInMsec();
		const int relativeTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( this, currentTime ) );
		if( relativeTime >= 0 && relativeTime < leadinLength )
		{
			volumeScale = soundShader->leadinVolume;
		}
	}

	volumeScale *= SoundSanitizeGainScale( s_volume.GetFloat(), 0.0f );
	volumeScale *= volumeFade.GetVolume( currentTime );
	volumeScale *= soundWorld->volumeFade.GetVolume( currentTime );
	volumeScale *= soundWorld->pauseFade.GetVolume( currentTime );
	if( parms.soundClass >= 0 && parms.soundClass < SOUND_MAX_CLASSES )
	{
		volumeScale *= soundWorld->soundClassFade[parms.soundClass].GetVolume( currentTime );
	}

	bool global = ( parms.soundShaderFlags & SSF_GLOBAL ) != 0;

	// attenuation
	if( !global && !emitterIsListener )
	{
		const bool noOcclusion = ( parms.soundShaderFlags & SSF_NO_OCCLUSION ) != 0 || !s_useOcclusion.GetBool();
		const float distance = noOcclusion ? emitter->directDistance : emitter->spatializedDistance;
		SoundWorldApplyDistanceFalloff( volumeScale, distance, parms.minDistance, parms.maxDistance );

		// OpenQ4 keeps the SDK's SSF_IS_VO name for bit 13; retail uses the same bit to bypass
		// this extra speaker/radio attenuation pass.
		if( ( parms.soundShaderFlags & SSF_IS_VO ) == 0 && ReceivesQuake4ExtraAttenuation( this ) )
		{
			if( IsRadioChatterChannel( this ) )
			{
				volumeScale *= SoundSanitizeUnitValue( s_radioChatterFraction.GetFloat(), 1.0f );
			}
			else
			{
				volumeScale *= SoundSanitizeUnitValue( s_speakerFraction.GetFloat(), 1.0f );
			}
		}
	}

	if( SoundUsesMusicVolume( parms ) )
	{
		volumeScale *= SoundSanitizeUnitValue( s_musicVolume.GetFloat(), 1.0f );
	}

	if( soundSystemLocal.musicMuted && SoundUsesMusicVolume( parms ) )
	{
		volumeScale = 0.0f;
	}

	volumeScale = SoundSanitizeGainScale( volumeScale, 0.0f );

	// store the new volume on the channel
	volumeDB = volumeScale < SOUND_WORLD_VOLUME_EPSILON ? DB_SILENCE : VolumeScaleToDB( volumeScale );

	// keep track of the maximum volume
	float currentVolumeDB = volumeDB;
	if( hardwareVoice != NULL )
	{
		float amplitude = hardwareVoice->GetAmplitude();
		if( amplitude <= 0.0f )
		{
			currentVolumeDB = DB_SILENCE;
		}
		else
		{
			currentVolumeDB += LinearToDB( amplitude );
		}
		currentAmplitude = amplitude;
	}
}

/*
========================
idSoundChannel::UpdateHardware
========================
*/
void idSoundChannel::UpdateHardware( float volumeAdd, int currentTime )
{
	idSoundWorldLocal* soundWorld = emitter->soundWorld;

	if( soundWorld == NULL )
	{
		return;
	}
	if( leadinSample == NULL )
	{
		return;
	}
	if( startTime > currentTime )
	{
		return;
	}
	if( SoundChannelHasCompleted( this, currentTime ) )
	{
		return;
	}
	const int playbackTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( this, currentTime ) );

	// convert volumes from decibels to linear
	const float mixedVolumeDB = volumeDB + volumeAdd;
	float volume = mixedVolumeDB <= DB_SILENCE ? 0.0f : Max( 0.0f, DBtoLinear( mixedVolumeDB ) );
	if( FLOAT_IS_NAN( volume ) )
	{
		volume = 0.0f;
	}
	volume = SoundSanitizeGainScale( volume, 0.0f );

	if( ( parms.soundShaderFlags & SSF_UNCLAMPED ) == 0 )
	{
		volume = Min( 1.0f, volume );
	}

	bool global = ( parms.soundShaderFlags & SSF_GLOBAL ) != 0;
	bool omni = ( parms.soundShaderFlags & SSF_OMNIDIRECTIONAL ) != 0;
	bool emitterIsListener = ( emitter->emitterId == soundWorld->listener.id );

	int startOffset = 0;
	bool issueStart = false;

	if( hardwareVoice == NULL )
	{
		if( volume <= 0.00001f )
		{
			return;
		}

		hardwareVoice = soundSystemLocal.AllocateVoice( leadinSample, loopingSample );

		if( hardwareVoice == NULL )
		{
			return;
		}

		issueStart = true;
		startOffset = playbackTime;
	}

	if( omni || global || emitterIsListener )
	{
		hardwareVoice->SetPosition( vec3_zero );
		hardwareVoice->SetVelocity( vec3_zero );
	}
	else
	{
		hardwareVoice->SetPosition( ( emitter->spatializedOrigin - soundWorld->listener.pos ) * soundWorld->listener.axis.Transpose() );
		if( ( parms.soundShaderFlags & SSF_USEDOPPLER ) != 0 )
		{
			// OpenAL expects relative sources to provide velocity in listener space too.
			hardwareVoice->SetVelocity( emitter->velocity * soundWorld->listener.axis.Transpose() );
		}
		else
		{
			hardwareVoice->SetVelocity( vec3_zero );
		}
	}
	if( parms.soundShaderFlags & SSF_CENTER )
	{
		hardwareVoice->SetCenterChannel( 1.0f );
	}
	else if( ( parms.soundShaderFlags & SSF_VO ) || ( parms.soundShaderFlags & SSF_IS_VO ) )
	{
		hardwareVoice->SetCenterChannel( s_centerFractionVO.GetFloat() );
	}
	else
	{
		hardwareVoice->SetCenterChannel( 0.0f );
	}

	extern idCVar com_timescale;

	hardwareVoice->SetGain( volume );
	hardwareVoice->SetInnerRadius( parms.minDistance * METERS_TO_DOOM );
	const float pitchScale = idMath::ClampFloat( 0.2f, 5.0f, SoundSanitizePositiveValue( com_timescale.GetFloat(), 1.0f ) );
	const float frequencyShift = SoundChannelFrequencyShift( this );
	const float wetLevel = SoundSanitizeUnitValue( parms.wetLevel, 0.0f );
	const float dryLevel = SoundSanitizeUnitValue( parms.dryLevel, 1.0f );
	hardwareVoice->SetWetLevel( wetLevel );
	hardwareVoice->SetDryLevel( dryLevel );
	hardwareVoice->SetPitch( SoundSanitizePositiveValue( soundWorld->slowmoSpeed, 1.0f ) * pitchScale * frequencyShift );

	float portalOcclusion = 0.0f;
	const bool allowPortalOcclusion = !global && !emitterIsListener && s_useOcclusion.GetBool() && ( parms.soundShaderFlags & SSF_NO_OCCLUSION ) == 0;
	if( allowPortalOcclusion && leadinSample->NumChannels() == 1 && emitter->occludingPortalCount > 0 )
	{
		portalOcclusion = SoundCombineOcclusion( portalOcclusion, SOUND_OCCLUSION_PER_BLOCKED_PORTAL * emitter->occludingPortalCount );
	}
	hardwareVoice->SetOcclusion( portalOcclusion );
	hardwareVoice->SetEnvironmentMuffle( soundWorld->enviroSuitActive ? SOUND_ENVIROSUIT_OCCLUSION : 0.0f );

	if( issueStart )
	{
		if( !hardwareVoice->Start( startOffset, parms.soundShaderFlags | ( parms.shakes == 0.0f ? SSF_NO_FLICKER : 0 ) ) )
		{
			soundSystemLocal.FreeVoice( hardwareVoice );
			hardwareVoice = NULL;
			if( !IsLooping() )
			{
				endTime = 1;
			}
		}
	}
	else
	{
		if( !hardwareVoice->Update() )
		{
			soundSystemLocal.FreeVoice( hardwareVoice );
			hardwareVoice = NULL;
			if( !IsLooping() )
			{
				endTime = 1;
			}
		}
	}
}

/*
================================================================================================

	idSoundEmitterLocal

================================================================================================
*/

/*
========================
idSoundEmitterLocal::idSoundEmitterLocal
========================
*/
idSoundEmitterLocal::idSoundEmitterLocal()
{
	Init( 0, NULL );
}

/*
========================
idSoundEmitterLocal::~idSoundEmitterLocal
========================
*/
idSoundEmitterLocal::~idSoundEmitterLocal()
{
	assert( channels.Num() == 0 );
}

/*
========================
idSoundEmitterLocal::Clear
========================
*/
void idSoundEmitterLocal::Init( int i, idSoundWorldLocal* sw )
{
	index = i;
	soundWorld = sw;

	// Init should only be called on a freshly constructed sound emitter or in a Reset()
	assert( channels.Num() == 0 );

	canFree = false;
	origin.Zero();
	velocity.Zero();
	emitterId = 0;

	directDistance = 0.0f;
	lastValidPortalArea = -1;
	spatializedDistance = 0.0f;
	spatializedOrigin.Zero();
	occludingPortalCount = 0;

	memset( &parms, 0, sizeof( parms ) );
	parms.subIndex = -1;

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_ALLOC_EMITTER );
		soundWorld->writeDemo->WriteInt( index );
	}
}

/*
========================
idSoundEmitterLocal::Reset
========================
*/
void idSoundEmitterLocal::Reset()
{
	for( int i = 0; i < channels.Num(); i++ )
	{
		soundWorld->FreeSoundChannel( channels[i] );
	}
	channels.Clear();
	Init( index, soundWorld );
}

/*
==================
idSoundEmitterLocal::OverrideParms
==================
*/
void idSoundEmitterLocal::OverrideParms( const soundShaderParms_t* base, const soundShaderParms_t* over, soundShaderParms_t* out )
{
	if( !over )
	{
		*out = *base;
		return;
	}
	if( over->minDistance )
	{
		out->minDistance = over->minDistance;
	}
	else
	{
		out->minDistance = base->minDistance;
	}
	if( over->maxDistance )
	{
		out->maxDistance = over->maxDistance;
	}
	else
	{
		out->maxDistance = base->maxDistance;
	}
	if( over->shakes )
	{
		out->shakes = over->shakes;
	}
	else
	{
		out->shakes = base->shakes;
	}
	if( over->volume )
	{
		out->volume = over->volume;
	}
	else
	{
		out->volume = base->volume;
	}
	if( over->attenuatedVolume )
	{
		out->attenuatedVolume = over->attenuatedVolume;
	}
	else
	{
		out->attenuatedVolume = base->attenuatedVolume;
	}
	if( over->subIndex >= 0 )
	{
		out->subIndex = over->subIndex;
	}
	else
	{
		out->subIndex = base->subIndex;
	}
	if( over->profanityIndex )
	{
		out->profanityIndex = over->profanityIndex;
	}
	else
	{
		out->profanityIndex = base->profanityIndex;
	}
	if( over->profanityDelay )
	{
		out->profanityDelay = over->profanityDelay;
	}
	else
	{
		out->profanityDelay = base->profanityDelay;
	}
	if( over->profanityDuration )
	{
		out->profanityDuration = over->profanityDuration;
	}
	else
	{
		out->profanityDuration = base->profanityDuration;
	}
	if( over->soundClass )
	{
		out->soundClass = over->soundClass;
	}
	else
	{
		out->soundClass = base->soundClass;
	}
	if( over->frequencyShift )
	{
		out->frequencyShift = over->frequencyShift;
	}
	else
	{
		out->frequencyShift = base->frequencyShift;
	}
	if( over->wetLevel )
	{
		out->wetLevel = over->wetLevel;
	}
	else
	{
		out->wetLevel = base->wetLevel;
	}
	if( over->dryLevel )
	{
		out->dryLevel = over->dryLevel;
	}
	else
	{
		out->dryLevel = base->dryLevel;
	}
	out->soundShaderFlags = base->soundShaderFlags | over->soundShaderFlags;
}

/*
========================
idSoundEmitterLocal::CheckForCompletion

Checks to see if any of the channels have completed, removing them as they do

This will also play any postSounds on the same channel as their owner.

Returns true if the emitter should be freed.
========================
*/
bool idSoundEmitterLocal::CheckForCompletion( int currentTime )
{
	for( int i = channels.Num() - 1; i >= 0 ; i-- )
	{
		idSoundChannel* chan = channels[i];

		if( chan->CheckForCompletion( currentTime ) )
		{
			channels.RemoveIndex( i );
			soundWorld->FreeSoundChannel( chan );
		}
	}
	return ( canFree && channels.Num() == 0 );
}

/*
========================
idSoundEmitterLocal::Update
========================
*/
void idSoundEmitterLocal::Update( int currentTime )
{
	if( channels.Num() == 0 )
	{
		return;
	}

	directDistance = ( soundWorld->listener.pos - origin ).LengthFast() * DOOM_TO_METERS;

	spatializedDistance = directDistance;
	spatializedOrigin = origin;
	occludingPortalCount = 0;

	// Initialize all channels to silence
	for( int i = 0; i < channels.Num(); i++ )
	{
		channels[i]->volumeDB = DB_SILENCE;
	}

	if( s_singleEmitter.GetInteger() > 0 && s_singleEmitter.GetInteger() != index )
	{
		return;
	}
	if( soundWorld->listener.area == -1 )
	{
		// listener is outside the world
		return;
	}
	if( soundSystemLocal.IsMuted() || soundWorld != soundSystemLocal.currentSoundWorld )
	{
		return;
	}
	float maxDistance = 0.0f;
	bool maxDistanceValid = false;
	bool useOcclusion = false;
	if( emitterId != soundWorld->listener.id )
	{
		for( int i = 0; i < channels.Num(); i++ )
		{
			idSoundChannel* chan = channels[i];
			if( ( chan->parms.soundShaderFlags & SSF_GLOBAL ) != 0 )
			{
				continue;
			}
			useOcclusion = useOcclusion || ( ( chan->parms.soundShaderFlags & SSF_NO_OCCLUSION ) == 0 );
			maxDistanceValid = true;
			if( maxDistance < channels[i]->parms.maxDistance )
			{
				maxDistance = channels[i]->parms.maxDistance;
			}
		}
	}
	if( maxDistanceValid && directDistance >= maxDistance )
	{
		// too far away to possibly hear it
		return;
	}
	if( useOcclusion && s_useOcclusion.GetBool() )
	{
		// work out virtual origin and distance, which may be from a portal instead of the actual origin
		if( soundWorld->renderWorld != NULL )
		{
			// we have a valid renderWorld
			int soundInArea = soundWorld->renderWorld->PointInArea( origin );
			if( soundInArea == -1 )
			{
				soundInArea = lastValidPortalArea;
			}
			else
			{
				lastValidPortalArea = soundInArea;
			}
			if( soundInArea != -1 && soundInArea != soundWorld->listener.area )
			{
				spatializedDistance = maxDistance * METERS_TO_DOOM;
				soundWorld->ResolveOrigin( 0, NULL, soundInArea, 0.0f, 0, origin, this );
				spatializedDistance *= DOOM_TO_METERS;
			}
		}
	}

	for( int j = 0; j < channels.Num(); j++ )
	{
		channels[j]->UpdateVolume( currentTime );
	}

	return;
}

/*
========================
idSoundEmitterLocal::Index
========================
*/
int idSoundEmitterLocal::Index() const
{
	assert( soundWorld );
	assert( soundWorld->emitters[this->index] == this );

	return index;
}

/*
========================
idSoundEmitterLocal::Free

Doesn't free it until the next update.
========================
*/
void idSoundEmitterLocal::Free( bool immediate )
{
	assert( soundWorld );
	assert( soundWorld->emitters[this->index] == this );

	if( canFree )
	{
		// Double free
		return;
	}
	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_FREE );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( immediate );
	}

	if( immediate )
	{
		Reset();
	}

	canFree = true;
}

/*
========================
idSoundEmitterLocal::UpdateEmitter
========================
*/
void idSoundEmitterLocal::UpdateEmitter( const idVec3& origin, int listenerId, const soundShaderParms_t* parms )
{
	UpdateEmitter( origin, vec3_zero, listenerId, parms );
}

/*
========================
idSoundEmitterLocal::UpdateEmitter
========================
*/
void idSoundEmitterLocal::UpdateEmitter( const idVec3& origin, const idVec3& velocity, int listenerId, const soundShaderParms_t* parms )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_UPDATE );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteVec3( origin );
		soundWorld->writeDemo->WriteVec3( velocity );
		soundWorld->writeDemo->WriteInt( listenerId );
		soundWorld->writeDemo->WriteFloat( parms->minDistance );
		soundWorld->writeDemo->WriteFloat( parms->maxDistance );
		soundWorld->writeDemo->WriteFloat( parms->volume );
		soundWorld->writeDemo->WriteFloat( parms->attenuatedVolume );
		soundWorld->writeDemo->WriteFloat( parms->shakes );
		soundWorld->writeDemo->WriteInt( parms->soundShaderFlags );
		soundWorld->writeDemo->WriteInt( parms->soundClass );
		soundWorld->writeDemo->WriteFloat( parms->frequencyShift );
		soundWorld->writeDemo->WriteFloat( parms->wetLevel );
		soundWorld->writeDemo->WriteFloat( parms->dryLevel );
	}

	this->origin = origin;
	this->velocity = velocity;
	this->emitterId = listenerId;
	this->parms = *parms;
}

/*
========================
idSoundEmitterLocal::StartSound

in most cases play sounds immediately, however
  intercept sounds using SSF_FINITE_SPEED_OF_SOUND
  and schedule them for playback later

return: int	- the length of the started sound in msec.
========================
*/
int idSoundEmitterLocal::StartSound( const idSoundShader* shader, const s_channelType channel, float diversity, int shaderFlags, bool allowSlow )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( shader == NULL || s_skipStartSound.GetBool() )
	{
		return 0;
	}

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_START );
		soundWorld->writeDemo->WriteInt( index );

		soundWorld->writeDemo->WriteHashString( shader->GetName() );

		soundWorld->writeDemo->WriteInt( channel );
		soundWorld->writeDemo->WriteFloat( diversity );
		soundWorld->writeDemo->WriteInt( shaderFlags );
	}

	if( s_noSound.GetBool() )
	{
		return 0;
	}

	int currentTime = soundWorld->GetSoundTime();

	bool showStartSound = s_showStartSound.GetBool();
	if( showStartSound )
	{
		idLib::Printf( "%dms: StartSound(%d:%d): %s: ", currentTime, index, channel, shader->GetName() );
	}

	// build the channel parameters by taking the shader parms and optionally overriding
	soundShaderParms_t	chanParms;
	chanParms = shader->parms;
	OverrideParms( &chanParms, &parms, &chanParms );
	chanParms.soundShaderFlags |= shaderFlags;

	if( shader->entries.Num() == 0 )
	{
		if( showStartSound )
		{
			idLib::Printf( S_COLOR_RED "No Entries\n" );
		}
		return 0;
	}

	// PLAY_ONCE sounds will never be restarted while they are running
	if( chanParms.soundShaderFlags & SSF_PLAY_ONCE )
	{
		for( int i = 0; i < channels.Num(); i++ )
		{
			idSoundChannel* chan = channels[i];
			if( chan->soundShader == shader && !chan->CheckForCompletion( currentTime ) )
			{
				if( showStartSound )
				{
					idLib::Printf( S_COLOR_YELLOW "Not started because of playOnce\n" );
				}
				return 0;
			}
		}
	}

	// never play the same sound twice with the same starting time, even
	// if they are on different channels
	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];
		if( chan->soundShader == shader && chan->startTime == currentTime && chan->endTime != 1 )
		{
			if( showStartSound )
			{
				idLib::Printf( S_COLOR_RED "Already started this frame\n" );
			}
			return 0;
		}
	}

	idSoundSample* leadinSample = NULL;
	idSoundSample* loopingSample = NULL;
	int choice = SoundSampleChoiceFromDiversity( shader->entries.Num(), diversity );

	if( ( chanParms.soundShaderFlags & SSF_NO_DUPS ) && shader->entries.Num() > 1 )
	{
		idSoundSample* selectedSample = NULL;
		if( choice < shader->leadins.Num() && shader->leadins[choice] != NULL )
		{
			selectedSample = shader->leadins[choice];
		}
		else
		{
			selectedSample = shader->entries[choice];
		}

		for( int i = 0; i < channels.Num(); i++ )
		{
			if( channels[i]->leadinSample == selectedSample )
			{
				choice = ( choice + 1 ) % shader->entries.Num();
				break;
			}
		}
	}

	// kill any sound that is currently playing on this channel after no_dups sees the outgoing sample
	if( channel != SCHANNEL_ANY )
	{
		for( int i = channels.Num() - 1; i >= 0; i-- )
		{
			idSoundChannel* chan = channels[i];
			if( chan->soundShader && chan->logicalChannel == channel )
			{
				if( showStartSound )
				{
					idLib::Printf( S_COLOR_YELLOW "OVERRIDE %s: ", chan->soundShader->GetName() );
				}
				channels.RemoveIndex( i );
				soundWorld->FreeSoundChannel( chan );
			}
		}
	}

	if( choice < shader->leadins.Num() && shader->leadins[choice] != NULL )
	{
		leadinSample = shader->leadins[choice];
	}
	else
	{
		leadinSample = shader->entries[choice];
	}
	leadinSample->SetLastPlayedTime( soundWorld->GetSoundTime() );

	if( chanParms.soundShaderFlags & SSF_LOOPING )
	{
		loopingSample = shader->entries[choice];
		if( loopingSample == NULL )
		{
			loopingSample = leadinSample;
		}
	}

	// set all the channel parameters here,
	// a hardware voice will be allocated next update if the volume is high enough to be audible
	if( channels.Num() == channels.Max() )
	{
		CheckForCompletion( currentTime );	// as a last chance try to release finished sounds here
		if( channels.Num() == channels.Max() )
		{
			if( showStartSound )
			{
				idLib::Printf( S_COLOR_RED "No free emitter channels!\n" );
			}
			return 0;
		}
	}
	idSoundChannel* chan = soundWorld->AllocSoundChannel();
	if( chan == NULL )
	{
		if( showStartSound )
		{
			idLib::Printf( S_COLOR_RED "No free global channels!\n" );
		}
		return 0;
	}
	channels.Append( chan );
	chan->emitter = this;
	chan->parms = chanParms;
	chan->soundShader = shader;
	chan->logicalChannel = channel;
	chan->choice = choice;
	chan->leadinSample = leadinSample;
	chan->loopingSample = loopingSample;
	chan->allowSlow = allowSlow;
	chan->lastFrequencyShiftTime = 0;
	chan->lastFrequencyShift = 1.0f;
	chan->elapsedFrequencyShiftTime = 0.0f;

	// return length of sound in milliseconds
	int length = chan->leadinSample->LengthInMsec();

	// adjust the start time based on diversity for looping sounds, so they don't all start at the same point
	int startOffset = 0;

	if( chan->IsLooping() && ( ( chanParms.soundShaderFlags & SSF_NO_RANDOMSTART ) == 0 ) )
	{
		// Retail applies diversity to every looping shader, including leadin-then-loop pairs.
		startOffset = SoundLoopStartOffsetFromDiversity( length, diversity );
	}

	chan->startTime = currentTime - startOffset;

	if( ( chanParms.soundShaderFlags & SSF_LOOPING ) != 0 )
	{
		// This channel will never end!
		chan->endTime = 0;
	}
	else
	{
		// This channel will automatically end at this time
		const float frequencyShift = SoundChannelFrequencyShift( chan );
		chan->endTime = chan->startTime + idMath::FtoiFast( length / frequencyShift ) + 100;
	}

	// Prey's authored bleep window mutes only the selected sample and restores
	// the pre-window scale afterwards. The modern mixer is linear, so the hold
	// target is exactly zero rather than a dB offset.
	if( IsProfanityCensorEnabled() && chanParms.profanityDuration > 0.0f && chanParms.profanityIndex == choice )
	{
		chan->volumeFade.FadeFrom( 0.0f,
			Max( 0, SEC2MS( chanParms.profanityDelay ) ),
			Max( 1, SEC2MS( chanParms.profanityDuration ) ),
			currentTime );
	}
	if( showStartSound )
	{
		if( loopingSample == NULL || leadinSample == loopingSample )
		{
			idLib::Printf( "Playing %s @ %d\n", leadinSample->GetName(), startOffset );
		}
		else
		{
			idLib::Printf( "Playing %s then looping %s\n", leadinSample->GetName(), loopingSample->GetName() );
		}
	}

	return length;
}


/*
========================
idSoundEmitterLocal::OnReloadSound

This is a shortened version of StartSound, called whenever a sound shader is reloaded.
If the emitter is currently playing the given sound shader, restart it so
a change in the sound sample used for a given sound shader will be picked up.
========================
*/
void idSoundEmitterLocal::OnReloadSound( const idDecl* decl )
{
}

/*
========================
idSoundEmitterLocal::StopSound

Can pass SCHANNEL_ANY.
========================
*/
void idSoundEmitterLocal::StopSound( const s_channelType channel )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_STOP );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( channel );
	}

	for( int i = channels.Num() - 1; i >= 0; i-- )
	{
		idSoundChannel* chan = channels[i];

		if( channel != SCHANNEL_ANY && chan->logicalChannel != channel )
		{
			continue;
		}
		if( s_showStartSound.GetBool() )
		{
			idLib::Printf( "%dms: StopSound(%d:%d): %s\n", soundWorld->GetSoundTime(), index, channel, chan->soundShader != NULL ? chan->soundShader->GetName() : "<null>" );
		}

		channels.RemoveIndex( i );
		soundWorld->FreeSoundChannel( chan );
	}
}

/*
========================
idSoundEmitterLocal::ModifySound
========================
*/
void idSoundEmitterLocal::ModifySound( const s_channelType channel, const soundShaderParms_t* parms )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld && soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_MODIFY );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( channel );
		soundWorld->writeDemo->WriteFloat( parms->minDistance );
		soundWorld->writeDemo->WriteFloat( parms->maxDistance );
		soundWorld->writeDemo->WriteFloat( parms->volume );
		soundWorld->writeDemo->WriteFloat( parms->attenuatedVolume );
		soundWorld->writeDemo->WriteFloat( parms->shakes );
		soundWorld->writeDemo->WriteInt( parms->soundShaderFlags );
		soundWorld->writeDemo->WriteInt( parms->soundClass );
		soundWorld->writeDemo->WriteFloat( parms->frequencyShift );
		soundWorld->writeDemo->WriteFloat( parms->wetLevel );
		soundWorld->writeDemo->WriteFloat( parms->dryLevel );
	}

	for( int i = channels.Num() - 1; i >= 0; i-- )
	{
		idSoundChannel* chan = channels[i];
		if( channel != SCHANNEL_ANY && chan->logicalChannel != channel )
		{
			continue;
		}
		if( s_showStartSound.GetBool() )
		{
			idLib::Printf( "%dms: ModifySound(%d:%d): %s\n", soundWorld->GetSoundTime(), index, channel, chan->soundShader->GetName() );
		}
		OverrideParms( &chan->parms, parms, &chan->parms );
	}
}

/*
========================
idSoundEmitterLocal::ModifySound
========================
*/
void idSoundEmitterLocal::ModifySound( idSoundShader* shader, const s_channelType channel, const hhSoundShaderParmsModifier& modifier )
{
	for( int i = channels.Num() - 1; i >= 0; i-- )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || ( channel != SCHANNEL_ANY && chan->logicalChannel != channel ) ||
			( shader != NULL && chan->soundShader != shader ) )
		{
			continue;
		}

		soundShaderParms_t modified = chan->parms;
		modifier.ModifyParms( modified );
		ModifySound( chan->logicalChannel, &modified );
		return;
	}
}

/*
========================
idSoundEmitterLocal::GetSoundParms
========================
*/
soundShaderParms_t* idSoundEmitterLocal::GetSoundParms( idSoundShader* shader, const s_channelType channel )
{
	static soundShaderParms_t convertedParms;
	for( int i = channels.Num() - 1; i >= 0; i-- )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || ( channel != SCHANNEL_ANY && chan->logicalChannel != channel ) ||
			( shader != NULL && chan->soundShader != shader ) )
		{
			continue;
		}

		convertedParms = chan->parms;
		convertedParms.volume = LinearToDB( chan->parms.volume );
		return &convertedParms;
	}
	return NULL;
}

/*
========================
idSoundEmitterLocal::FadeSound
========================
*/
void idSoundEmitterLocal::FadeSound( const s_channelType channel, float to, float over )
{
	assert( soundWorld != NULL );
	assert( soundWorld->emitters[this->index] == this );

	if( soundWorld->writeDemo )
	{
		soundWorld->writeDemo->WriteInt( DS_SOUND );
		soundWorld->writeDemo->WriteInt( SCMD_FADE );
		soundWorld->writeDemo->WriteInt( index );
		soundWorld->writeDemo->WriteInt( channel );
		soundWorld->writeDemo->WriteFloat( to );
		soundWorld->writeDemo->WriteFloat( over );
	}

	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];

		if( channel != SCHANNEL_ANY && chan->logicalChannel != channel )
		{
			continue;
		}
		if( s_showStartSound.GetBool() )
		{
			idLib::Printf( "%dms: FadeSound(%d:%d): %s to %.2fdb over %.2f seconds\n", soundWorld->GetSoundTime(), index, channel, chan->soundShader->GetName(), to, over );
		}

		// fade it
		chan->volumeFade.FadeDB( to, over, soundWorld->GetSoundTime() );
	}
}

/*
========================
idSoundEmitterLocal::CurrentlyPlaying
========================
*/
bool idSoundEmitterLocal::CurrentlyPlaying( const s_channelType channel ) const
{
	for( int i = 0; i < channels.Num(); ++i )
	{
		if( channels[i] == NULL || channels[i]->soundShader == NULL || channels[i]->endTime == 1 )
		{
			continue;
		}
		if( channel == SCHANNEL_ANY || channels[i]->logicalChannel == channel )
		{
			return true;
		}
	}

	return false;
}

/*
========================
idSoundEmitterLocal::CurrentAmplitude
========================
*/
float idSoundEmitterLocal::CurrentAmplitude()
{
	if( s_constantAmplitude.GetFloat() >= 0.0f )
	{
		return s_constantAmplitude.GetFloat();
	}

	float high = 0.0f;
	float sampleAmplitude = 0.0f;
	const int currentTime = soundWorld->GetSoundTime();
	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || currentTime < chan->startTime || SoundChannelHasCompleted( chan, currentTime ) )
		{
			continue;
		}

		if( ( chan->parms.soundShaderFlags & SSF_NO_FLICKER ) != 0 )
		{
			high = Max( high, 32767.0f );
			continue;
		}

		const int relativeTime = idMath::FtoiFast( SoundChannelPlaybackTimeMsec( chan, currentTime ) );
		const char* shakeData = chan->soundShader != NULL ? chan->soundShader->GetShakeData( chan->choice ) : "";
		if( shakeData != NULL && shakeData[0] != '\0' )
		{
			int shakeDataLength = 0;
			while( shakeData[shakeDataLength] != '\0' )
			{
				shakeDataLength++;
			}

			int shakeIndex = SOUND_SHADER_SHAKE_RATE_HZ * relativeTime / 1000;
			if( ( chan->parms.soundShaderFlags & SSF_LOOPING ) != 0 )
			{
				shakeIndex %= shakeDataLength;
			}
			if( shakeIndex >= 0 && shakeIndex < shakeDataLength )
			{
				const int shakeValue = Max( 0, shakeData[shakeIndex] - 'a' );
				high = Max( high, shakeValue * SOUND_SHADER_MATERIAL_SHAKE_SCALE );
			}
			continue;
		}

		if( chan->leadinSample == NULL )
		{
			continue;
		}
		const int leadinLength = chan->leadinSample->LengthInMsec();
		if( relativeTime < leadinLength )
		{
			sampleAmplitude = Max( sampleAmplitude, chan->leadinSample->GetAmplitude( relativeTime ) );
		}
		else if( chan->loopingSample != NULL && chan->loopingSample->LengthInMsec() > 0 )
		{
			sampleAmplitude = Max( sampleAmplitude, chan->loopingSample->GetAmplitude( ( relativeTime - leadinLength ) % chan->loopingSample->LengthInMsec() ) );
		}
	}
	if( high > 0.0f )
	{
		return idMath::ATan( high * SOUND_SHADER_SHAKE_NORMALIZE, 1.0f ) / DEG2RAD( 45.0f );
	}
	return sampleAmplitude;
}

float idSoundEmitterLocal::CurrentAmplitude( const s_channelType channel )
{
	if( channel == SCHANNEL_ANY )
	{
		return CurrentAmplitude();
	}

	float amplitude = 0.0f;
	const int currentTime = soundWorld->GetSoundTime();
	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || chan->logicalChannel != channel || currentTime < chan->startTime || SoundChannelHasCompleted( chan, currentTime ) )
		{
			continue;
		}
		if( ( chan->parms.soundShaderFlags & SSF_NO_FLICKER ) != 0 )
		{
			return 1.0f;
		}
		if( chan->hardwareVoice != NULL )
		{
			amplitude = Max( amplitude, chan->hardwareVoice->GetAmplitude() );
		}
	}
	return amplitude;
}

float idSoundEmitterLocal::CurrentVoiceAmplitude( const s_channelType channel )
{
	float amplitude = 0.0f;
	const int currentTime = soundWorld->GetSoundTime();
	for( int i = 0; i < channels.Num(); i++ )
	{
		idSoundChannel* chan = channels[i];
		if( chan == NULL || ( channel != SCHANNEL_ANY && chan->logicalChannel != channel ) ||
			( chan->parms.soundShaderFlags & SSF_VOICEAMPLITUDE ) == 0 ||
			currentTime < chan->startTime || SoundChannelHasCompleted( chan, currentTime ) )
		{
			continue;
		}
		if( ( chan->parms.soundShaderFlags & SSF_NO_FLICKER ) != 0 )
		{
			return 1.0f;
		}
		if( chan->hardwareVoice != NULL )
		{
			amplitude = Max( amplitude, chan->hardwareVoice->GetAmplitude() );
		}
	}
	return amplitude;
}
