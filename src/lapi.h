/*
** $Id: lapi.h,v 2.7 2009/11/27 15:37:59 roberto Exp $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"

inline void adjustresults(lua_State* L, int nres) {
  if ((nres == LUA_MULTRET) && (L->stack_.callinfo_->getTop() < L->stack_.top_)) {
    L->stack_.callinfo_->setTop(L->stack_.top_);
  }
}

#endif
