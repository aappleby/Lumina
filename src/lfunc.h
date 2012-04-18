/*
** $Id: lfunc.h,v 2.6 2010/06/04 13:06:15 roberto Exp $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


UpVal* luaF_findupval (StkId level);

const char *luaF_getlocalname (const Proto *func, int local_number, int pc);


#endif
