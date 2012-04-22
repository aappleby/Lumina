/*
** $Id: lcorolib.c,v 1.3 2011/08/23 17:24:34 roberto Exp $
** Coroutine Library
** See Copyright Notice in lua.h
*/

#include "LuaState.h"

#include <stdlib.h>


#define lcorolib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK



static int auxresume (lua_State *L, lua_State *co, int narg) {
  THREAD_CHECK(L);
  int status;

  int checkresult;
  {
    THREAD_CHANGE(co);
    checkresult = lua_checkstack(co, narg);
  }
  if (!checkresult) {
    lua_pushliteral(L, "too many arguments to resume");
    return -1;  /* error flag */
  }
  if (co->status == LUA_OK && co->stack_.getTopIndex() == 0) {
    lua_pushliteral(L, "cannot resume dead coroutine");
    return -1;  /* error flag */
  }
  lua_xmove(L, co, narg);
  {
    THREAD_CHANGE(co);
    status = lua_resume(co, L, narg);
  }
  if (status == LUA_OK || status == LUA_YIELD) {
    int nres;
    {
      THREAD_CHANGE(co);
      nres = co->stack_.getTopIndex();
    }
    if (!lua_checkstack(L, nres + 1)) {
      {
        THREAD_CHANGE(co);
        co->stack_.pop(nres);  /* remove results anyway */
      }
      lua_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    {
      THREAD_CHANGE(co);
      lua_xmove(co, L, nres);  /* move yielded values */
    }
    return nres;
  }
  else {
    THREAD_CHANGE(co);
    lua_xmove(co, L, 1);  /* move error message */
    return -1;  /* error flag */
  }
}


static int luaB_coresume (lua_State *L) {
  THREAD_CHECK(L);
  lua_State *co = lua_tothread(L, 1);
  int r;
  luaL_argcheck(L, co, 1, "coroutine expected");
  r = auxresume(L, co, L->stack_.getTopIndex() - 1);
  if (r < 0) {
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lua_pushboolean(L, 1);
    lua_insert(L, -(r + 1));
    return r + 1;  /* return true + `resume' returns */
  }
}


static int luaB_auxwrap (lua_State *L) {
  THREAD_CHECK(L);
  lua_State *co = lua_tothread(L, lua_upvalueindex(1));
  int r = auxresume(L, co, L->stack_.getTopIndex());
  if (r < 0) {
    if (lua_isStringable(L, -1)) {  /* error object is a string? */
      luaL_where(L, 1);  /* add extra info */
      lua_insert(L, -2);
      lua_concat(L, 2);
    }
    lua_error(L);  /* propagate error */
  }
  return r;
}


static int luaB_cocreate (lua_State *L) {
  THREAD_CHECK(L);
  lua_State *NL = lua_newthread(L);
  luaL_checkIsFunction(L, 1);
  L->stack_.copy(1);  /* move function to top */
  lua_xmove(L, NL, 1);  /* move function from L to NL */
  return 1;
}


static int luaB_cowrap (lua_State *L) {
  THREAD_CHECK(L);
  luaB_cocreate(L);
  lua_pushcclosure(L, luaB_auxwrap, 1);
  return 1;
}


static int luaB_yield (lua_State *L) {
  THREAD_CHECK(L);
  return lua_yield(L, L->stack_.getTopIndex());
}


static int luaB_costatus (lua_State *L) {
  THREAD_CHECK(L);
  lua_State *co = lua_tothread(L, 1);
  luaL_argcheck(L, co, 1, "coroutine expected");
  if (L == co) lua_pushliteral(L, "running");
  else {
    switch (co->status) {
      case LUA_YIELD:
        lua_pushliteral(L, "suspended");
        break;
      case LUA_OK: {
        THREAD_CHANGE(co);
        lua_Debug ar;
        if (lua_getstack(co, 0, &ar) > 0) {  /* does it have frames? */
          THREAD_CHANGE(L);
          lua_pushliteral(L, "normal");  /* it is running */
        }
        else if (co->stack_.getTopIndex() == 0) {
          THREAD_CHANGE(L);
          lua_pushliteral(L, "dead");
        }
        else {
          THREAD_CHANGE(L);
          lua_pushliteral(L, "suspended");  /* initial state */
        }
        break;
      }
      default:  /* some error occurred */
        lua_pushliteral(L, "dead");
        break;
    }
  }
  return 1;
}


static int luaB_corunning (lua_State *L) {
  THREAD_CHECK(L);
  int ismain = lua_pushthread(L);
  lua_pushboolean(L, ismain);
  return 2;
}


static const luaL_Reg co_funcs[] = {
  {"create", luaB_cocreate},
  {"resume", luaB_coresume},
  {"running", luaB_corunning},
  {"status", luaB_costatus},
  {"wrap", luaB_cowrap},
  {"yield", luaB_yield},
  {NULL, NULL}
};



int luaopen_coroutine (lua_State *L) {
  THREAD_CHECK(L);
  lua_createtable(L, 0, sizeof(co_funcs)/sizeof((co_funcs)[0]) - 1);
  luaL_setfuncs(L,co_funcs,0);
  return 1;
}

