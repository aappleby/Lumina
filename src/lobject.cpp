/*
** $Id: lobject.c,v 2.55 2011/11/30 19:30:16 roberto Exp $
** Some generic functions over Lua objects
** See Copyright Notice in lua.h
*/

#include "LuaConversions.h"
#include "LuaGlobals.h"
#include "LuaState.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>

#define lobject_c

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lvm.h"



double luaO_arith (int op, double v1, double v2) {
  switch (op) {
    case LUA_OPADD: return v1 + v2;
    case LUA_OPSUB: return v1 - v2;
    case LUA_OPMUL: return v1 * v2;
    case LUA_OPDIV: return v1 / v2;
    case LUA_OPMOD: return v1 - floor(v1/v2)*v2;
    case LUA_OPPOW: return pow(v1,v2);
    case LUA_OPUNM: return -v1;
    default: assert(0); return 0;
  }
}

void pushstr(LuaThread* L, const std::string& s) {
  THREAD_CHECK(L);
  LuaString* s2 = thread_G->strings_->Create(s.c_str(), s.size());
  LuaResult result = L->stack_.push_reserve2(s2);
  handleResult(result);
}

static void pushstr (LuaThread *L, const char *str, int l) {
  THREAD_CHECK(L);
  LuaString* s = thread_G->strings_->Create(str, l);
  LuaResult result = L->stack_.push_reserve2(s);
  handleResult(result);
}

const char *luaO_pushvfstring (const char *fmt, va_list argp) {
  LuaThread* L = thread_L;

  std::string result2;
  StringVprintf(fmt, argp, result2);

  pushstr(L, result2.c_str(), result2.size());

  return L->stack_.top_[-1].getString()->c_str();
}

const char *luaO_pushfstring (LuaThread *L, const char *fmt, ...) {
  THREAD_CHECK(L);
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = luaO_pushvfstring(fmt, argp);
  va_end(argp);
  return msg;
}

