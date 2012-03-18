/*
** $Id: ldo.h,v 2.20 2011/11/29 15:55:08 roberto Exp $
** Stack and Call structure of Lua
** See Copyright Notice in lua.h
*/

#ifndef ldo_h
#define ldo_h


#include "lobject.h"
#include "lstate.h"
#include "lzio.h"


#define luaD_checkstack(L,n)	if (L->stack_last - L->top <= (n)) luaD_growstack(L, n);


#define incr_top(L) {L->top++; luaD_checkstack(L,0);}

#define savestack(L,p)		((char *)(p) - (char *)L->stack.begin())
#define restorestack(L,n)	((TValue *)((char *)L->stack.begin() + (n)))


/* type of protected functions, to be ran by `runprotected' */
typedef void (*Pfunc) (lua_State *L, void *ud);

int luaD_protectedparser (lua_State *L, ZIO *z, const char *name,
                                                  const char *mode);
void luaD_hook (lua_State *L, int event, int line);
int luaD_precall (lua_State *L, StkId func, int nresults);
void luaD_call (lua_State *L, StkId func, int nResults,
                                        int allowyield);
int luaD_pcall (lua_State *L, Pfunc func, void *u,
                                        ptrdiff_t oldtop, ptrdiff_t ef);
int luaD_poscall (lua_State *L, StkId firstResult);
void luaD_reallocstack (lua_State *L, int newsize);
void luaD_growstack (lua_State *L, int n);
void luaD_shrinkstack (lua_State *L);

l_noret luaD_throw (int errcode);
int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud);

#endif

