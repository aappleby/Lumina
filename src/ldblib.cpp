/*
** $Id: ldblib.c,v 1.131 2011/10/24 14:54:05 roberto Exp $
** Interface from Lua to its debug API
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ldblib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK



#define HOOKKEY		"_HKEY"



static int db_getregistry (LuaThread *L) {
  THREAD_CHECK(L);
  L->stack_.push(thread_G->l_registry);
  return 1;
}


static int db_getmetatable (LuaThread *L) {
  THREAD_CHECK(L);
  luaL_checkany(L, 1);
  if (!lua_getmetatable(L, 1)) {
    L->stack_.push(LuaValue::Nil());  /* no metatable */
  }
  return 1;
}


static int db_setmetatable (LuaThread *L) {
  THREAD_CHECK(L);
  LuaValue v = L->stack_.at(2);
  luaL_argcheck(L,
                v.isNil() ||
                v.isTable(),
                2,
                "nil or table expected");
  L->stack_.setTopIndex(2);
  lua_setmetatable(L, 1);
  return 1;  /* return 1st argument */
}


static int db_getuservalue (LuaThread *L) {
  THREAD_CHECK(L);

  LuaValue arg = L->stack_.at(1);

  if(arg.isBlob() && arg.getBlob()->env_) {
    L->stack_.push( LuaValue(arg.getBlob()->env_) );
  }
  else {
    L->stack_.push(LuaValue::Nil());
  }

  return 1;
}


static int db_setuservalue (LuaThread *L) {
  THREAD_CHECK(L);

  LuaValue arg1 = L->stack_.at(1);
  LuaValue arg2 = L->stack_.at(2);
  L->stack_.pop();

  if(arg1.isPointer()) {
    luaL_argerror(L, 1, "full userdata expected, got light userdata");
  }

  if(!arg1.isBlob()) {
    luaL_argerror(L, 1, "full userdata expected");
  }

  if(!arg2.isTable() && !arg2.isNil()) {
    luaL_argerror(L, 2, "expected table or Nil");
  }

  LuaBlob* blob = arg1.getBlob();
  blob->env_ = arg2.isTable() ? arg2.getTable() : NULL;
  return 1;
}


static LuaThread *getthread (LuaThread *L, int *arg) {
  THREAD_CHECK(L);
  if (lua_isthread(L, 1)) {
    *arg = 1;
    return lua_tothread(L, 1);
  }
  else {
    *arg = 0;
    return L;
  }
}


static void treatstackoption (LuaThread *L, LuaThread *L1, const char *fname) {
  THREAD_CHECK(L);
  if (L == L1) {
    L->stack_.copy(-2);
    L->stack_.remove(-3);
  }
  else {
    THREAD_CHANGE(L1);
    lua_xmove(L1, L, 1);
  }
  lua_setfield(L, -2, fname);
}


static int db_getinfo (LuaThread *L) {
  THREAD_CHECK(L);
  LuaDebug ar;
  int arg;
  LuaThread *L1 = getthread(L, &arg);
  const char *options = luaL_optstring(L, arg+2, "flnStu");

  if (lua_isNumberable(L, arg+1)) {
    int idx = (int)lua_tointeger(L, arg+1);
    int result;
    {
      THREAD_CHANGE(L1);
      result = lua_getstack(L1, idx, &ar);
    }
    if (!result) {
      L->stack_.push(LuaValue::Nil());  /* level out of range */
      return 1;
    }
  }
  else if (lua_isfunction(L, arg+1)) {
    lua_pushfstring(L, ">%s", options);
    options = lua_tostring(L, -1);
    L->stack_.copy(arg+1);
    lua_xmove(L, L1, 1);
  }
  else {
    return luaL_argerror(L, arg+1, "function or level expected");
  }

  int result;
  {
    THREAD_CHANGE(L1);
    result = lua_getinfo(L1, options, &ar);
  }
  
  if (!result) {
    return luaL_argerror(L, arg+2, "invalid option");
  }

  LuaTable* t = new LuaTable(0, 2);
  L->stack_.push(LuaValue(t));
 
  t->set("source",          ar.source2.c_str());
  t->set("short_src",       ar.short_src2.c_str());
  t->set("linedefined",     LuaValue(ar.linedefined) );
  t->set("lastlinedefined", LuaValue(ar.lastlinedefined) );
  t->set("what",            ar.what2.c_str());
  t->set("currentline",     LuaValue(ar.currentline));
  t->set("nups",            LuaValue(ar.nups));
  t->set("nparams",         LuaValue(ar.nparams));
  t->set("isvararg",        LuaValue(ar.isvararg));
  t->set("name",            ar.name2.empty() ? NULL : ar.name2.c_str());
  t->set("namewhat",        ar.namewhat2.c_str());
  t->set("istailcall",      LuaValue(ar.istailcall));

  if (strchr(options, 'L'))
    treatstackoption(L, L1, "activelines");
  if (strchr(options, 'f'))
    treatstackoption(L, L1, "func");

  return 1;  /* return table */
}


static int db_getlocal (LuaThread *L) {
  THREAD_CHECK(L);
  int arg;
  LuaThread *L1 = getthread(L, &arg);
  LuaDebug ar;
  const char *name;
  int nvar = luaL_checkint(L, arg+2);  /* local-variable index */
  if (lua_isfunction(L, arg + 1)) {  /* function argument? */
    L->stack_.copy(arg + 1);  /* push function */
    lua_pushstring(L, lua_getlocal(L, NULL, nvar));  /* push local name */
    return 1;
  }
  else {  /* stack-level argument */
    int idx = luaL_checkint(L, arg+1);
    int result;
    {
      THREAD_CHANGE(L1);
      result = lua_getstack(L1, idx, &ar);
    }
    if (!result)  /* out of range? */
      return luaL_argerror(L, arg+1, "level out of range");
    {
      THREAD_CHANGE(L1);
      name = lua_getlocal(L1, &ar, nvar);
    }
    if (name) {
      {
        THREAD_CHANGE(L1);
        lua_xmove(L1, L, 1);  /* push local value */
      }
      lua_pushstring(L, name);  /* push name */
      L->stack_.copy(-2);  /* re-order */
      return 2;
    }
    else {
      L->stack_.push(LuaValue::Nil());  /* no name (nor value) */
      return 1;
    }
  }
}


static int db_setlocal (LuaThread *L) {
  THREAD_CHECK(L);
  int arg;
  LuaThread *L1 = getthread(L, &arg);
  LuaDebug ar;
  int idx = luaL_checkint(L, arg+1);
  int result;
  {
    THREAD_CHANGE(L1);
    result = lua_getstack(L1, idx, &ar);
  }
  if (!result)  /* out of range? */
    return luaL_argerror(L, arg+1, "level out of range");
  luaL_checkany(L, arg+3);
  L->stack_.setTopIndex(arg+3);
  lua_xmove(L, L1, 1);
  idx = luaL_checkint(L, arg+2);
  const char * result2;
  {
    THREAD_CHANGE(L1);
    result2 = lua_setlocal(L1, &ar, idx);
  }
  lua_pushstring(L, result2);
  return 1;
}


static int auxupvalue (LuaThread *L, int get) {
  THREAD_CHECK(L);
  const char *name;
  int n = luaL_checkint(L, 2);
  luaL_checkIsFunction(L, 1);
  name = get ? lua_getupvalue(L, 1, n) : lua_setupvalue(L, 1, n);
  if (name == NULL) return 0;
  lua_pushstring(L, name);
  lua_insert(L, -(get+1));
  return get + 1;
}


static int db_getupvalue (LuaThread *L) {
  THREAD_CHECK(L);
  return auxupvalue(L, 1);
}


static int db_setupvalue (LuaThread *L) {
  THREAD_CHECK(L);
  luaL_checkany(L, 3);
  return auxupvalue(L, 0);
}


static int checkupval (LuaThread *L, int argf, int argnup) {
  THREAD_CHECK(L);
  LuaDebug ar;
  int nup = luaL_checkint(L, argnup);
  luaL_checkIsFunction(L, argf);
  L->stack_.copy(argf);
  lua_getinfo(L, ">u", &ar);
  luaL_argcheck(L, 1 <= nup && nup <= ar.nups, argnup, "invalid upvalue index");
  return nup;
}


static int db_upvalueid (LuaThread *L) {
  THREAD_CHECK(L);
  int n = checkupval(L, 1, 2);
  lua_pushlightuserdata(L, lua_upvalueid(L, 1, n));
  return 1;
}


static int db_upvaluejoin (LuaThread *L) {
  THREAD_CHECK(L);
  int n1 = checkupval(L, 1, 2);
  int n2 = checkupval(L, 3, 4);
  luaL_argcheck(L, !lua_iscfunction(L, 1), 1, "Lua function expected");
  luaL_argcheck(L, !lua_iscfunction(L, 3), 3, "Lua function expected");
  lua_upvaluejoin(L, 1, n1, 3, n2);
  return 0;
}


#define gethooktable(L)	luaL_getregistrytable(L, HOOKKEY);


static void hookf (LuaThread *L, LuaDebug *ar) {
  THREAD_CHECK(L);
  static const char *const hooknames[] =
    {"call", "return", "line", "count", "tail call"};
  gethooktable(L);
  lua_rawgetp(L, -1, L);
  if (lua_isfunction(L, -1)) {
    lua_pushstring(L, hooknames[(int)ar->event]);
    if (ar->currentline >= 0)
      lua_pushinteger(L, ar->currentline);
    else L->stack_.push(LuaValue::Nil());
    assert(lua_getinfo(L, "lS", ar));
    lua_call(L, 2, 0);
  }
}


static int makemask (const char *smask, int count) {
  int mask = 0;
  if (strchr(smask, 'c')) mask |= LUA_MASKCALL;
  if (strchr(smask, 'r')) mask |= LUA_MASKRET;
  if (strchr(smask, 'l')) mask |= LUA_MASKLINE;
  if (count > 0) mask |= LUA_MASKCOUNT;
  return mask;
}


static char *unmakemask (int mask, char *smask) {
  int i = 0;
  if (mask & LUA_MASKCALL) smask[i++] = 'c';
  if (mask & LUA_MASKRET) smask[i++] = 'r';
  if (mask & LUA_MASKLINE) smask[i++] = 'l';
  smask[i] = '\0';
  return smask;
}


static int db_sethook (LuaThread *L) {
  THREAD_CHECK(L);
  int arg, mask, count;
  LuaHook func;
  LuaThread *L1 = getthread(L, &arg);
  if (lua_isnoneornil(L, arg+1)) {
    L->stack_.setTopIndex(arg+1);
    func = NULL; mask = 0; count = 0;  /* turn off hooks */
  }
  else {
    const char *smask = luaL_checkstring(L, arg+2);
    luaL_checkIsFunction(L, arg+1);
    count = luaL_optint(L, arg+3, 0);
    func = hookf; mask = makemask(smask, count);
  }
  gethooktable(L);
  L->stack_.copy(arg+1);
  lua_rawsetp(L, -2, L1);  /* set new hook */
  L->stack_.pop();  /* remove hook table */
  {
    THREAD_CHANGE(L1);
    lua_sethook(L1, func, mask, count);  /* set hooks */
  }
  return 0;
}


static int db_gethook (LuaThread *L) {
  THREAD_CHECK(L);
  int arg;
  LuaThread *L1 = getthread(L, &arg);
  char buff[5];
  int mask;
  LuaHook hook;
  {
    THREAD_CHANGE(L1);
    mask = lua_gethookmask(L1);
    hook = lua_gethook(L1);
  }
  if (hook != NULL && hook != hookf) {
    // external hook?
    lua_pushliteral(L, "external hook");
  }
  else {
    gethooktable(L);
    lua_rawgetp(L, -1, L1);   /* get hook */
    L->stack_.remove(-2);  /* remove hook table */
  }
  lua_pushstring(L, unmakemask(mask, buff));
  int hookcount;
  {
    THREAD_CHANGE(L1);
    hookcount = lua_gethookcount(L1);
  }
  lua_pushinteger(L, hookcount);
  return 3;
}


static int db_debug (LuaThread *L) {
  THREAD_CHECK(L);
  for (;;) {
    char buffer[250];
    luai_writestringerror("%s", "lua_debug> ");
    if (fgets(buffer, sizeof(buffer), stdin) == 0 ||
        strcmp(buffer, "cont\n") == 0)
      return 0;
    if (luaL_loadbuffer(L, buffer, strlen(buffer), "=(debug command)") ||
        lua_pcall(L, 0, 0, 0))
      luai_writestringerror("%s\n", lua_tostring(L, -1));
    L->stack_.setTopIndex(0);  /* remove eventual returns */
  }
}


static int db_traceback (LuaThread *L) {
  THREAD_CHECK(L);
  int arg;
  LuaThread *L1 = getthread(L, &arg);
  const char *msg = lua_tostring(L, arg + 1);
  if (msg == NULL && !lua_isnoneornil(L, arg + 1))  /* non-string 'msg'? */
    L->stack_.copy(arg + 1);  /* return it untouched */
  else {
    int level = luaL_optint(L, arg + 2, (L == L1) ? 1 : 0);
    luaL_traceback(L, L1, msg, level);
  }
  return 1;
}


static const luaL_Reg dblib[] = {
  {"debug", db_debug},
  {"getuservalue", db_getuservalue},
  {"gethook", db_gethook},
  {"getinfo", db_getinfo},
  {"getlocal", db_getlocal},
  {"getregistry", db_getregistry},
  {"getmetatable", db_getmetatable},
  {"getupvalue", db_getupvalue},
  {"upvaluejoin", db_upvaluejoin},
  {"upvalueid", db_upvalueid},
  {"setuservalue", db_setuservalue},
  {"sethook", db_sethook},
  {"setlocal", db_setlocal},
  {"setmetatable", db_setmetatable},
  {"setupvalue", db_setupvalue},
  {"traceback", db_traceback},
  {NULL, NULL}
};


int luaopen_debug (LuaThread *L) {
  THREAD_CHECK(L);

  lua_createtable(L, 0, sizeof(dblib)/sizeof((dblib)[0]) - 1);
  luaL_setfuncs(L,dblib,0);

  return 1;
}

