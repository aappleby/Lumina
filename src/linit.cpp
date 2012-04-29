/*
** $Id: linit.c,v 1.32 2011/04/08 19:17:36 roberto Exp $
** Initialization of libraries for lua.c and other clients
** See Copyright Notice in lua.h
*/


/*
** If you embed Lua in your program and need to open the standard
** libraries, call luaL_openlibs in your program. If you need a
** different set of libraries, copy this file to your project and edit
** it to suit your needs.
*/

#include "LuaGlobals.h"
#include "LuaState.h"

#define LUA_LIB

#include "lua.h"

#include "lualib.h"
#include "lauxlib.h"
#include "lstate.h" // for THREAD_CHECK


/*
** these libs are loaded by lua.c and are readily available to any Lua
** program
*/
static const luaL_Reg loadedlibs[] = {
  {"_G", luaopen_base},
  {LUA_LOADLIBNAME, luaopen_package},
  {LUA_COLIBNAME, luaopen_coroutine},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_IOLIBNAME, luaopen_io},
  {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_BITLIBNAME, luaopen_bit32},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_DBLIBNAME, luaopen_debug},
  {LUA_TESTLIBNAME, luaopen_test},
  {NULL, NULL}
};


void luaL_openlibs (LuaThread *L) {
  THREAD_CHECK(L);

  LuaTable* loadedModules = L->l_G->getRegistryTable("_LOADED");
  LuaTable* globals = L->l_G->getGlobals();

  /* call open functions from 'loadedlibs' and set results to global table */
  for (const luaL_Reg* lib = loadedlibs; lib->func; lib++) {
      
    lib->func(L);
    loadedModules->set(lib->name, L->stack_.at(-1) );
    globals->set(lib->name, L->stack_.at(-1) );

    L->stack_.pop();  /* remove lib */
  }
}

