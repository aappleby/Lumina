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
  L->stack_.top_++;
  api_check(L->stack_.top_ <= L->stack_.callinfo_->top, "stack overflow");
}

inline void adjustresults(lua_State* L, int nres) {
  if ((nres == LUA_MULTRET) && (L->stack_.callinfo_->top < L->stack_.top_)) {
    L->stack_.callinfo_->top = L->stack_.top_;
  }
}

void api_checknelems(lua_State* L, int n);


#endif
