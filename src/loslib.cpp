/*
** $Id: loslib.c,v 1.38 2011/11/30 12:35:05 roberto Exp $
** Standard Operating System library
** See Copyright Notice in lua.h
*/

#include "LuaState.h"

#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define loslib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK


/*
** list of valid conversion specifiers for the 'strftime' function
*/
#if !defined(LUA_STRFTIMEOPTIONS)

#if !defined(LUA_USE_POSIX)
#define LUA_STRFTIMEOPTIONS     { "aAbBcdHIjmMpSUwWxXyYz%", "" }
#else
#define LUA_STRFTIMEOPTIONS     { "aAbBcCdDeFgGhHIjmMnprRStTuUVwWxXyYzZ%", "", \
                                "E", "cCxXyY",  \
                                "O", "deHImMSuUVwWy" }
#endif

#endif



/*
** By default, Lua uses tmpnam except when POSIX is available, where it
** uses mkstemp.
*/
#if defined(LUA_USE_MKSTEMP)
#include <unistd.h>
#define LUA_TMPNAMBUFSIZE       32
#define lua_tmpnam(b,e) { \
        strcpy(b, "/tmp/lua_XXXXXX"); \
        e = mkstemp(b); \
        if (e != -1) close(e); \
        e = (e == -1); }

#elif !defined(lua_tmpnam)

#define LUA_TMPNAMBUFSIZE       L_tmpnam
#define lua_tmpnam(b,e)         { e = (tmpnam(b) == NULL); }

#endif


/*
** By default, Lua uses gmtime/localtime, except when POSIX is available,
** where it uses gmtime_r/localtime_r
*/
#if defined(LUA_USE_GMTIME_R)

#define l_gmtime(t,r)		gmtime_r(t,r)
#define l_localtime(t,r)	localtime_r(t,r)

#elif !defined(l_gmtime)

#define l_gmtime(t,r)		((void)r, gmtime(t))
#define l_localtime(t,r)  	((void)r, localtime(t))

#endif



static int os_execute (LuaThread *L) {
  THREAD_CHECK(L);
  const char *cmd = luaL_optstring(L, 1, NULL);
  int stat = system(cmd);
  if (cmd != NULL)
    return luaL_execresult(L, stat);
  else {
    lua_pushboolean(L, stat);  /* true if there is a shell */
    return 1;
  }
}


static int os_remove (LuaThread *L) {
  THREAD_CHECK(L);
  const char *filename = luaL_checkstring(L, 1);
  return luaL_fileresult(L, remove(filename) == 0, filename);
}


static int os_rename (LuaThread *L) {
  THREAD_CHECK(L);
  const char *fromname = luaL_checkstring(L, 1);
  const char *toname = luaL_checkstring(L, 2);
  return luaL_fileresult(L, rename(fromname, toname) == 0, fromname);
}


static int os_tmpname (LuaThread *L) {
  THREAD_CHECK(L);
  char buff[LUA_TMPNAMBUFSIZE];
  int err;
  lua_tmpnam(buff, err);
  if (err) {
    return luaL_error(L, "unable to generate a unique filename");
  }
  lua_pushstring(L, buff);
  return 1;
}


static int os_getenv (LuaThread *L) {
  THREAD_CHECK(L);
  lua_pushstring(L, getenv(luaL_checkstring(L, 1)));  /* if NULL push nil */
  return 1;
}


static int os_clock (LuaThread *L) {
  THREAD_CHECK(L);
  lua_pushnumber(L, ((double)clock())/(double)CLOCKS_PER_SEC);
  return 1;
}


/*
** {======================================================
** Time/Date operations
** { year=%Y, month=%m, day=%d, hour=%H, min=%M, sec=%S,
**   wday=%w+1, yday=%j, isdst=? }
** =======================================================
*/

static void setfield (LuaThread *L, const char *key, int value) {
  THREAD_CHECK(L);
  lua_pushinteger(L, value);
  lua_setfield(L, -2, key);
}

static void setboolfield (LuaThread *L, const char *key, int value) {
  THREAD_CHECK(L);
  if (value < 0)  /* undefined? */
    return;  /* does not set field */
  lua_pushboolean(L, value);
  lua_setfield(L, -2, key);
}

static int getboolfield (LuaThread *L, const char *key) {
  THREAD_CHECK(L);
  int res;
  lua_getfield(L, -1, key);
  res = lua_isnil(L, -1) ? -1 : lua_toboolean(L, -1);
  L->stack_.pop();
  return res;
}


static int getfield (LuaThread *L, const char *key, int d) {
  THREAD_CHECK(L);
  int res, isnum;
  lua_getfield(L, -1, key);
  res = (int)lua_tointegerx(L, -1, &isnum);
  if (!isnum) {
    if (d < 0)
      return luaL_error(L, "field " LUA_QS " missing in date table", key);
    res = d;
  }
  L->stack_.pop();
  return res;
}


static const char *checkoption (LuaThread *L, const char *conv, char *buff) {
  THREAD_CHECK(L);
  static const char *const options[] = LUA_STRFTIMEOPTIONS;
  unsigned int i;
  for (i = 0; i < sizeof(options)/sizeof(options[0]); i += 2) {
    if (*conv != '\0' && strchr(options[i], *conv) != NULL) {
      buff[1] = *conv;
      if (*options[i + 1] == '\0') {  /* one-char conversion specifier? */
        buff[2] = '\0';  /* end buffer */
        return conv + 1;
      }
      else if (*(conv + 1) != '\0' &&
               strchr(options[i + 1], *(conv + 1)) != NULL) {
        buff[2] = *(conv + 1);  /* valid two-char conversion specifier */
        buff[3] = '\0';  /* end buffer */
        return conv + 2;
      }
    }
  }
  const char* text = lua_pushfstring(L, "invalid conversion specifier '%%%s'", conv);
  luaL_argerror(L, 1, text);
  return conv;  /* to avoid warnings */
}


static int os_date (LuaThread *L) {
  THREAD_CHECK(L);
  const char *s = luaL_optstring(L, 1, "%c");
  time_t t = luaL_opt(L, (time_t)luaL_checknumber, 2, time(NULL));
  struct tm tmr, *stm;
  if (*s == '!') {  /* UTC? */
    stm = l_gmtime(&t, &tmr);
    s++;  /* skip `!' */
  }
  else
    stm = l_localtime(&t, &tmr);
  if (stm == NULL) {
    /* invalid date? */
    L->stack_.push(LuaValue::Nil());
  }
  else if (strcmp(s, "*t") == 0) {
    lua_createtable(L, 0, 9);  /* 9 = number of fields */
    setfield(L, "sec", stm->tm_sec);
    setfield(L, "min", stm->tm_min);
    setfield(L, "hour", stm->tm_hour);
    setfield(L, "day", stm->tm_mday);
    setfield(L, "month", stm->tm_mon+1);
    setfield(L, "year", stm->tm_year+1900);
    setfield(L, "wday", stm->tm_wday+1);
    setfield(L, "yday", stm->tm_yday+1);
    setboolfield(L, "isdst", stm->tm_isdst);
  }
  else {
    char cc[4];
    luaL_Buffer b;
    cc[0] = '%';
    luaL_buffinit(L, &b);
    while (*s) {
      if (*s != '%')  /* no conversion specifier? */
        luaL_addchar(&b, *s++);
      else {
        size_t reslen;
        char buff[200];  /* should be big enough for any conversion result */
        s = checkoption(L, s + 1, cc);
        reslen = strftime(buff, sizeof(buff), cc, stm);
        luaL_addlstring(&b, buff, reslen);
      }
    }
    luaL_pushresult(&b);
  }
  return 1;
}


static int os_time (LuaThread *L) {
  THREAD_CHECK(L);
  time_t t;
  if (lua_isnoneornil(L, 1))  /* called without args? */
    t = time(NULL);  /* get current time */
  else {
    struct tm ts;
    luaL_checktype(L, 1, LUA_TTABLE);
    L->stack_.setTopIndex(1);  /* make sure table is at the top */
    ts.tm_sec = getfield(L, "sec", 0);
    ts.tm_min = getfield(L, "min", 0);
    ts.tm_hour = getfield(L, "hour", 12);
    ts.tm_mday = getfield(L, "day", -1);
    ts.tm_mon = getfield(L, "month", -1) - 1;
    ts.tm_year = getfield(L, "year", -1) - 1900;
    ts.tm_isdst = getboolfield(L, "isdst");
    t = mktime(&ts);
  }
  if (t == (time_t)(-1))
    L->stack_.push(LuaValue::Nil());
  else
    lua_pushnumber(L, (double)t);
  return 1;
}


static int os_difftime (LuaThread *L) {
  THREAD_CHECK(L);
  lua_pushnumber(L, difftime((time_t)(luaL_checknumber(L, 1)),
                             (time_t)(luaL_optnumber(L, 2, 0))));
  return 1;
}

/* }====================================================== */


static int os_setlocale (LuaThread *L) {
  THREAD_CHECK(L);
  static const int cat[] = {LC_ALL, LC_COLLATE, LC_CTYPE, LC_MONETARY,
                      LC_NUMERIC, LC_TIME};
  static const char *const catnames[] = {"all", "collate", "ctype", "monetary",
     "numeric", "time", NULL};
  const char *l = luaL_optstring(L, 1, NULL);
  int op = luaL_checkoption(L, 2, "all", catnames);
  lua_pushstring(L, setlocale(cat[op], l));
  return 1;
}


static int os_exit (LuaThread *L) {
  THREAD_CHECK(L);
  int status;
  if (lua_isboolean(L, 1))
    status = (lua_toboolean(L, 1) ? EXIT_SUCCESS : EXIT_FAILURE);
  else
    status = luaL_optint(L, 1, EXIT_SUCCESS);
  if (lua_toboolean(L, 2))
    lua_close(L);
  if (L) exit(status);  /* 'if' to avoid warnings for unreachable 'return' */
  return 0;
}


static const luaL_Reg syslib[] = {
  {"clock",     os_clock},
  {"date",      os_date},
  {"difftime",  os_difftime},
  {"execute",   os_execute},
  {"exit",      os_exit},
  {"getenv",    os_getenv},
  {"remove",    os_remove},
  {"rename",    os_rename},
  {"setlocale", os_setlocale},
  {"time",      os_time},
  {"tmpname",   os_tmpname},
  {NULL, NULL}
};

/* }====================================================== */



int luaopen_os (LuaThread *L) {
  THREAD_CHECK(L);

  LuaTable* lib = new LuaTable();
  for(const luaL_Reg* cursor = syslib; cursor->name; cursor++) {
    lib->set( cursor->name, LuaValue(cursor->func) );
  }
  L->stack_.push(lib);

  return 1;
}

