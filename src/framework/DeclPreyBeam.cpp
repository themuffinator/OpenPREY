#include "../idlib/precompiled.h"
#pragma hdrstop

#include "declPreyBeam.h"

namespace {

static const int PREY_MAX_BEAM_NODES = 32;

enum preyBeamQuadSlot_t {
	PREY_BEAM_QUAD_ORIGIN = 0,
	PREY_BEAM_QUAD_END = 1
};

static bool ParseBeamVec3( idLexer &src, idVec3 &value ) {
	bool error = false;

	if ( !src.ExpectTokenString( "(" ) ) {
		return false;
	}

	value.x = src.ParseFloat( &error );
	if ( error ) {
		return false;
	}
	src.CheckTokenString( "," );

	value.y = src.ParseFloat( &error );
	if ( error ) {
		return false;
	}
	src.CheckTokenString( "," );

	value.z = src.ParseFloat( &error );
	if ( error ) {
		return false;
	}

	return src.ExpectTokenString( ")" ) != 0;
}

static bool ParseBeamCommand( idLexer &src, const idToken &token, beamCmd_t &cmd ) {
	if ( !token.Icmp( "SplineLinearToTarget" ) ) {
		cmd.type = BEAMCMD_SplineLinearToTarget;
		return true;
	}
	if ( !token.Icmp( "SplineArcToTarget" ) ) {
		cmd.type = BEAMCMD_SplineArcToTarget;
		return true;
	}
	if ( !token.Icmp( "ConvertSplineToNodes" ) ) {
		cmd.type = BEAMCMD_ConvertSplineToNodes;
		return true;
	}
	if ( !token.Icmp( "NodeLinearToTarget" ) ) {
		cmd.type = BEAMCMD_NodeLinearToTarget;
		return true;
	}
	if ( !token.Icmp( "NodeElectric" ) ) {
		cmd.type = BEAMCMD_NodeElectric;
		return ParseBeamVec3( src, cmd.offset );
	}
	if ( !token.Icmp( "SplineAdd" ) ) {
		cmd.type = BEAMCMD_SplineAdd;
		cmd.index = src.ParseInt();
		return ParseBeamVec3( src, cmd.offset );
	}
	if ( !token.Icmp( "SplineAddSin" ) ) {
		cmd.type = BEAMCMD_SplineAddSin;
		cmd.index = src.ParseInt();
		return ParseBeamVec3( src, cmd.phase ) && ParseBeamVec3( src, cmd.offset );
	}
	if ( !token.Icmp( "SplineAddSinTime" ) ) {
		cmd.type = BEAMCMD_SplineAddSinTim;
		cmd.index = src.ParseInt();
		return ParseBeamVec3( src, cmd.phase ) && ParseBeamVec3( src, cmd.offset );
	}
	if ( !token.Icmp( "SplineAddSinTimeScaled" ) ) {
		cmd.type = BEAMCMD_SplineAddSinTimeScaled;
		cmd.index = src.ParseInt();
		return ParseBeamVec3( src, cmd.phase ) && ParseBeamVec3( src, cmd.offset );
	}

	return false;
}

} // namespace

hhDeclBeam::hhDeclBeam() {
	FreeData();
}

const char *hhDeclBeam::DefaultDefinition() const {
	return
		"{\n"
	"\t" "numNodes 2\n"
	"\t" "numBeams 1\n"
	"\t" "_default {\n"
	"\t\t" "thickness 1\n"
	"\t\t" "NodeLinearToTarget\n"
	"\t" "}\n"
		"}";
}

void hhDeclBeam::FreeData() {
	numNodes = 2;
	numBeams = 1;

	for ( int i = 0; i < MAX_BEAMS; ++i ) {
		thickness[i] = 1.0f;
		bFlat[i] = false;
		bTaperEndPoints[i] = false;
		shader[i] = NULL;
		quadShader[i][PREY_BEAM_QUAD_ORIGIN] = NULL;
		quadShader[i][PREY_BEAM_QUAD_END] = NULL;
		quadSize[i][PREY_BEAM_QUAD_ORIGIN] = 0.0f;
		quadSize[i][PREY_BEAM_QUAD_END] = 0.0f;
		cmds[i].Clear();
	}
}

bool hhDeclBeam::Parse( const char *text, const int textLength ) {
	idLexer src;
	idToken token;
	const idMaterial *defaultShader = declManager->FindMaterial( "_default" );
	int parsedBeams = 0;

	FreeData();

	src.LoadMemory( text, textLength, GetFileName(), GetLineNum() );
	src.SetFlags( DECL_LEXER_FLAGS );
	src.SkipUntilString( "{" );

	while ( src.ReadToken( &token ) ) {
		if ( token == "}" ) {
			break;
		}

		if ( !token.Icmp( "numNodes" ) ) {
			numNodes = idMath::ClampInt( 2, PREY_MAX_BEAM_NODES, src.ParseInt() );
			continue;
		}

		if ( !token.Icmp( "numBeams" ) ) {
			numBeams = idMath::ClampInt( 1, MAX_BEAMS, src.ParseInt() );
			continue;
		}

		if ( !src.ExpectTokenString( "{" ) ) {
			MakeDefault();
			return false;
		}

		const int beamIndex = parsedBeams;
		const bool storeBeam = beamIndex < MAX_BEAMS;
		if ( storeBeam ) {
			shader[beamIndex] = declManager->FindMaterial( token.c_str() );
		} else {
			src.Warning( "beam decl '%s' has more than %d beams, extras ignored", GetName(), MAX_BEAMS );
		}

		while ( src.ReadToken( &token ) ) {
			if ( token == "}" ) {
				break;
			}

			if ( !token.Icmp( "thickness" ) ) {
				const float value = src.ParseFloat();
				if ( storeBeam ) {
					thickness[beamIndex] = value;
				}
				continue;
			}

			if ( !token.Icmp( "flat" ) ) {
				const bool value = src.ParseBool();
				if ( storeBeam ) {
					bFlat[beamIndex] = value;
				}
				continue;
			}

			if ( !token.Icmp( "taperEnds" ) || !token.Icmp( "taperEndPoints" ) ) {
				const bool value = src.ParseBool();
				if ( storeBeam ) {
					bTaperEndPoints[beamIndex] = value;
				}
				continue;
			}

			if ( !token.Icmp( "quadOriginShader" ) || !token.Icmp( "quadoriginShader" ) ) {
				idToken shaderToken;
				if ( !src.ReadToken( &shaderToken ) ) {
					MakeDefault();
					return false;
				}
				if ( storeBeam ) {
					quadShader[beamIndex][PREY_BEAM_QUAD_ORIGIN] = declManager->FindMaterial( shaderToken.c_str() );
				}
				continue;
			}

			if ( !token.Icmp( "quadEndShader" ) ) {
				idToken shaderToken;
				if ( !src.ReadToken( &shaderToken ) ) {
					MakeDefault();
					return false;
				}
				if ( storeBeam ) {
					quadShader[beamIndex][PREY_BEAM_QUAD_END] = declManager->FindMaterial( shaderToken.c_str() );
				}
				continue;
			}

			if ( !token.Icmp( "quadOriginSize" ) || !token.Icmp( "quadoriginSize" ) ) {
				const float value = src.ParseFloat();
				if ( storeBeam ) {
					quadSize[beamIndex][PREY_BEAM_QUAD_ORIGIN] = value;
				}
				continue;
			}

			if ( !token.Icmp( "quadEndSize" ) ) {
				const float value = src.ParseFloat();
				if ( storeBeam ) {
					quadSize[beamIndex][PREY_BEAM_QUAD_END] = value;
				}
				continue;
			}

			beamCmd_t cmd;
			memset( &cmd, 0, sizeof( cmd ) );
			if ( ParseBeamCommand( src, token, cmd ) ) {
				if ( storeBeam ) {
					cmds[beamIndex].Append( cmd );
				}
				continue;
			}

			src.Warning( "Unknown beam token '%s' in decl '%s'", token.c_str(), GetName() );
			src.SkipRestOfLine();
		}

		parsedBeams++;
	}

	if ( parsedBeams > 0 ) {
		numBeams = idMath::ClampInt( 1, MAX_BEAMS, parsedBeams );
	}

	for ( int i = 0; i < numBeams; ++i ) {
		if ( shader[i] == NULL ) {
			shader[i] = defaultShader;
		}
		if ( cmds[i].Num() == 0 ) {
			beamCmd_t cmd;
			memset( &cmd, 0, sizeof( cmd ) );
			cmd.type = BEAMCMD_NodeLinearToTarget;
			cmds[i].Append( cmd );
		}
	}

	if ( src.HadError() ) {
		src.Warning( "beam decl '%s' had a parse error", GetName() );
		MakeDefault();
		return false;
	}

	return true;
}
