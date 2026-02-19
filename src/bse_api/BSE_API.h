#ifndef _BSE_API_H_INC_
#define _BSE_API_H_INC_

#include "BSEInterface.h"

const int BSE_API_VERSION = 2;

class idSys;
class idCommon;
class idCmdSystem;
class idCVarSystem;
class idFileSystem;
class idNetworkSystem;
class idRenderSystem;
class idSoundSystem;
class idRenderModelManager;
class idUserInterfaceManager;
class idDeclManager;
class idAASFileManager;
class idCollisionModelManager;
class idGame;
class idGameEdit;
class idSession;
class idDecl;

typedef void (*BSE_SetRuntimePointers_t)(idGame* game, idGameEdit* gameEdit, idSession* session);
typedef idDecl* (*BSE_AllocDeclEffect_t)(void);

struct bseImport_t {
	int						version;
	idSys*						sys;
	idCommon*					common;
	idCmdSystem*				cmdSystem;
	idCVarSystem*				cvarSystem;
	idFileSystem*				fileSystem;
	idNetworkSystem*			networkSystem;
	idRenderSystem*				renderSystem;
	idSoundSystem*				soundSystem;
	idRenderModelManager*		renderModelManager;
	idUserInterfaceManager*		uiManager;
	idDeclManager*				declManager;
	idAASFileManager*			AASFileManager;
	idCollisionModelManager*	collisionModelManager;
	idGame*						game;
	idGameEdit*					gameEdit;
	idSession*					session;
};

struct bseExport_t {
	int						version;
	rvBSEManager*				bse;
	rvDeclEffectEdit*			declEffectEdit;
	BSE_SetRuntimePointers_t	SetRuntimePointers;
	BSE_AllocDeclEffect_t		AllocDeclEffect;
};

extern "C" {
typedef bseExport_t* (*GetBSEAPI_t)(bseImport_t* import);
}

extern BSE_AllocDeclEffect_t	bseAllocDeclEffect;

#endif // _BSE_API_H_INC_
