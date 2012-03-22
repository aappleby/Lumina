/*
** $Id: lfunc.h,v 2.6 2010/06/04 13:06:15 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


Closure*  luaF_newCclosure (int nelems);
Closure*  luaF_newLclosure (Proto *p);
UpVal*    luaF_newupval    ();

void luaF_freeclosure (Closure *c);
void luaF_freeupval   (UpVal *uv);

UpVal* luaF_findupval (StkId level);

void luaF_close       (StkId level);

const char *luaF_getlocalname (const Proto *func, int local_number, int pc);


#endif
