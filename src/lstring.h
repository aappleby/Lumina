/*
** $Id: lstring.h,v 1.46 2010/04/05 16:26:37 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define sizeudata(u)	(sizeof(Udata)+(u)->len)

#define luaS_newliteral(s)	(luaS_newlstr("" s, (sizeof(s)/sizeof(char))-1))

void     luaS_resize   (int newsize);
TString* luaS_newlstr  (const char *str, size_t l);
TString* luaS_new      (const char *str);
void     luaS_freestr  (TString* ts);

Udata*   luaS_newudata (size_t s, Table *e);
void     luaS_deludata (Udata* ud);

void luaS_initstrt();
void luaS_freestrt();


#endif
