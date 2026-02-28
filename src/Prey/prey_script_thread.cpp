#include "../idlib/precompiled.h"
#pragma hdrstop

#include "prey_local.h"

CLASS_DECLARATION( idThread, hhThread )
END_CLASS

hhThread::hhThread() : idThread() {}
hhThread::hhThread( idEntity *self, const function_t *func ) : idThread( self, func ) {}
hhThread::hhThread( const function_t *func ) : idThread( func ) {}
hhThread::hhThread( idInterpreter *source, const function_t *func, int args ) : idThread( source, func, args ) {}

/*
================
hhThread::PushParm
================
*/
void hhThread::PushParm( int value ) {
	interpreter.Push( value );
}

/*
================
hhThread::PushString
================
*/
void hhThread::PushString( const char *text ) {
	interpreter.PushStringValue( text ? text : "" );
}

/*
================
hhThread::PushFloat
================
*/
void hhThread::PushFloat( float value ) {
	int packed = 0;
	memcpy( &packed, &value, sizeof( packed ) );
	PushParm( packed );
}

/*
================
hhThread::PushInt
================
*/
void hhThread::PushInt( int value ) {
	PushParm( value );
}

/*
================
hhThread::PushVector
================
*/
void hhThread::PushVector( const idVec3 &vec ) {
	for( int ix = 0; ix < vec.GetDimension(); ++ix ) {
		float value = vec[ ix ];
		int packed = 0;
		memcpy( &packed, &value, sizeof( packed ) );
		PushParm( packed );
	}
}

/*
================
hhThread::PushEntity
================
*/
void hhThread::PushEntity( const idEntity *ent ) {
	HH_ASSERT( ent );

	PushParm( ent->entityNumber + 1 );
}

/*
================
hhThread::ClearStack
================
*/
void hhThread::ClearStack() {
	interpreter.Reset();
}

/*
================
hhThread::ParseAndPushArgsOntoStack
================
*/
bool hhThread::ParseAndPushArgsOntoStack( const idCmdArgs &args, const function_t* function ) {
	idList<idStr>	parmList;

	hhUtils::SplitString( args, parmList );

	return ParseAndPushArgsOntoStack( parmList, function );
}

/*
================
hhThread::ParseAndPushArgsOntoStack
================
*/
bool hhThread::ParseAndPushArgsOntoStack( const idList<idStr>& args, const function_t* function ) {
	if( !function || !function->def || !function->def->TypeDef() ) {
		gameLocal.Warning( "hhThread::ParseAndPushArgsOntoStack called with invalid function" );
		return false;
	}

	int numParms = function->def->TypeDef()->NumParameters();
	if( args.Num() < numParms ) {
		gameLocal.Warning( "hhThread::ParseAndPushArgsOntoStack expected %d parms, got %d for '%s'",
			numParms, args.Num(), function->Name() );
		return false;
	}

	idTypeDef* parmType = NULL;
	const char* parm = NULL;

	for( int ix = 0; ix < numParms; ++ix ) {
		parmType = function->def->TypeDef()->GetParmType( ix );
		if( !parmType ) {
			gameLocal.Warning( "hhThread::ParseAndPushArgsOntoStack missing parm type %d for '%s'",
				ix, function->Name() );
			return false;
		}

		parm = args[ ix ].c_str();

		parmType->PushOntoStack( parm, this );
	}

	return true;
}
