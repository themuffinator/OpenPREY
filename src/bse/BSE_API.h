#ifndef _BSE_API_H_INC_
#define _BSE_API_H_INC_

#include "BSEInterface.h"

class idDecl;

typedef idDecl* (*BSE_AllocDeclEffect_t)(void);

rvBSEManager*				openPREY_GetIntegratedBSEManager( void );
rvDeclEffectEdit*			openPREY_GetIntegratedBSEDeclEffectEdit( void );
idDecl*						openPREY_AllocIntegratedBSEDeclEffect( void );
bool						openPREY_IsIntegratedBSEDeclEffect( const idDecl *decl );

extern BSE_AllocDeclEffect_t	bseAllocDeclEffect;

#endif // _BSE_API_H_INC_
