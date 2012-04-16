/*
** $Id: lapi.h,v 2.7 2009/11/27 15:37:59 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"

inline void api_incr_top(lua_State* L) {
  L->top++;
  api_check(L->top <= L->callinfo_->top, "stack overflow");
}

inline void adjustresults(lua_State* L, int nres) {
  if ((nres == LUA_MULTRET) && (L->callinfo_->top < L->top)) {
    L->callinfo_->top = L->top;
  }
}

void api_checknelems(lua_State* L, int n);


#endif
