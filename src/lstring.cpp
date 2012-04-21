/*
** $Id: lstring.c,v 2.19 2011/05/03 16:01:57 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaUserdata.h"

#include <string.h>

#include "MurmurHash3.h"

#include "lua.h"

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ldebug.h"

void luaS_resize(int newsize) {
  thread_G->strings_->resize(newsize);
}

TString *luaS_newlstr (const char *str, size_t l) {
  return TString::Create(str,l);
}

TString *luaS_new (const char *str) {
  return TString::Create(str);
}
