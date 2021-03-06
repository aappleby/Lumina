/*
** $Id: loadlib.c,v 1.108 2011/12/12 16:34:03 roberto Exp $
** Dynamic library loader for Lua
** See Copyright Notice in lua.h
**
** This module contains an implementation of loadlib for Unix systems
** that have dlfcn, an implementation for Windows, and a stub for other
** systems.
*/

#include "LuaClosure.h"
#include "LuaGlobals.h"
#include "LuaState.h"
#include "LuaUserdata.h"

/*
** if needed, includes windows header before everything else
*/
#if defined(_WIN32)
#include <windows.h>
#endif


#include <stdlib.h>
#include <string.h>


#define loadlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK

std::string replace_all (const char* source,
                         const char* pattern,
                         const char* replace);


/*
** LUA_PATH_SEP is the character that separates templates in a path.
** LUA_PATH_MARK is the string that marks the substitution points in a
** template.
** LUA_EXEC_DIR in a Windows path is replaced by the executable's
** directory.
** LUA_IGMARK is a mark to ignore all before it when building the
** luaopen_ function name.
*/
#if !defined (LUA_PATH_SEP)
#define LUA_PATH_SEP		";"
#endif
#if !defined (LUA_PATH_MARK)
#define LUA_PATH_MARK		"?"
#endif
#if !defined (LUA_EXEC_DIR)
#define LUA_EXEC_DIR		"!"
#endif
#if !defined (LUA_IGMARK)
#define LUA_IGMARK		"-"
#endif


/*
** LUA_CSUBSEP is the character that replaces dots in submodule names
** when searching for a C loader.
** LUA_LSUBSEP is the character that replaces dots in submodule names
** when searching for a Lua loader.
*/
#if !defined(LUA_CSUBSEP)
#define LUA_CSUBSEP		LUA_DIRSEP
#endif

#if !defined(LUA_LSUBSEP)
#define LUA_LSUBSEP		LUA_DIRSEP
#endif


/* prefix for open functions in C libraries */
#define LUA_POF		"luaopen_"

/* separator for open functions in C libraries */
#define LUA_OFSEP	"_"


#define LIBPREFIX	"LOADLIB: "

#define POF		LUA_POF
#define LIB_FAIL	"open"


/* error codes for ll_loadfunc */
#define ERRLIB		1
#define ERRFUNC		2

#define setprogdir(L)		((void)0)


/*
** system-dependent functions
*/
static void ll_unloadlib (void *lib);
static void *ll_load (LuaThread *L, const char *path, int seeglb);
static LuaCallback ll_sym (LuaThread *L, void *lib, const char *sym);



#if defined(LUA_USE_DLOPEN)
/*
** {========================================================================
** This is an implementation of loadlib based on the dlfcn interface.
** The dlfcn interface is available in Linux, SunOS, Solaris, IRIX, FreeBSD,
** NetBSD, AIX 4.2, HPUX 11, and  probably most other Unix flavors, at least
** as an emulation layer on top of native functions.
** =========================================================================
*/

#include <dlfcn.h>

static void ll_unloadlib (void *lib) {
  dlclose(lib);
}


static void *ll_load (LuaThread *L, const char *path, int seeglb) {
  void *lib = dlopen(path, RTLD_NOW | (seeglb ? RTLD_GLOBAL : RTLD_LOCAL));
  if (lib == NULL) {
    lua_pushstring(L, dlerror());
  }
  return lib;
}


static LuaCallback ll_sym (LuaThread *L, void *lib, const char *sym) {
  LuaCallback f = (LuaCallback)dlsym(lib, sym);
  if (f == NULL) {
    lua_pushstring(L, dlerror());
  }
  return f;
}

/* }====================================================== */



#elif defined(LUA_DL_DLL)
/*
** {======================================================================
** This is an implementation of loadlib for Windows using native functions.
** =======================================================================
*/

/*
** optional flags for LoadLibraryEx
*/
#if !defined(LUA_LLE_FLAGS)
#define LUA_LLE_FLAGS	0
#endif

std::string GetModuleDirectory() {
  char buff[256];
  buff[0] = 0;
  GetModuleFileNameA(NULL, buff, 256);

  char* lb = strrchr(buff, '\\');

  if(lb == NULL) return "";
  *lb = 0;
  return std::string(buff);
}

static void pusherror (LuaThread *L) {
  THREAD_CHECK(L);
  int error = GetLastError();
  char buffer[128];
  if (FormatMessageA(FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL, error, 0, buffer, sizeof(buffer)/sizeof(char), NULL)) {
    lua_pushstring(L, buffer);
  }
  else {
    lua_pushfstring(L, "system error %d\n", error);
  }
}

static void ll_unloadlib (void *lib) {
  FreeLibrary((HMODULE)lib);
}


static void *ll_load (LuaThread *L, const char *path, int seeglb) {
  THREAD_CHECK(L);
  HMODULE lib = LoadLibraryExA(path, NULL, LUA_LLE_FLAGS);
  (void)(seeglb);  /* not used: symbols are 'global' by default */
  if (lib == NULL) pusherror(L);
  return lib;
}


static LuaCallback ll_sym (LuaThread *L, void *lib, const char *sym) {
  THREAD_CHECK(L);
  LuaCallback f = (LuaCallback)GetProcAddress((HMODULE)lib, sym);
  if (f == NULL) pusherror(L);
  return f;
}

/* }====================================================== */


#else
/*
** {======================================================
** Fallback for other systems
** =======================================================
*/

#undef LIB_FAIL
#define LIB_FAIL	"absent"


#define DLMSG	"dynamic libraries not enabled; check your Lua installation"


static void ll_unloadlib (void *lib) {
  (void)(lib);  /* not used */
}


static void *ll_load (LuaThread *L, const char *path, int seeglb) {
  (void)(path); (void)(seeglb);  /* not used */
  lua_pushliteral(L, DLMSG);
  return NULL;
}


static LuaCallback ll_sym (LuaThread *L, void *lib, const char *sym) {
  (void)(lib); (void)(sym);  /* not used */
  lua_pushliteral(L, DLMSG);
  return NULL;
}

/* }====================================================== */
#endif



static void **ll_register (LuaThread *L, const char *path) {
  THREAD_CHECK(L);
  void **plib;
  lua_pushfstring(L, "%s%s", LIBPREFIX, path);

  LuaValue key = L->stack_.top_[-1];
  L->stack_.pop();

  LuaTable* registry = L->l_G->getRegistry();
  LuaValue val = registry->get(key);

  if (!val.isNil() && !val.isNone()) {
    /* is there an entry? */
    L->stack_.push(val);
    plib = (void**)val.getBlob()->buf_;
  }
  else {  /* no entry yet; create one */
    LuaBlob* newBlob = new LuaBlob(sizeof(void*));

    plib = (void **)newBlob->buf_;
    *plib = NULL;

    // TODO(aappleby): Intentionally trying to break this code by not setting
    // the metatable doesn't work. What is this doing and how can we test it?

    LuaValue meta = registry->get("_LOADLIB");
    newBlob->metatable_ = meta.getTable();
    
    lua_pushfstring(L, "%s%s", LIBPREFIX, path);
    LuaValue key = L->stack_.top_[-1];
    L->stack_.pop();

    registry->set(key, LuaValue(newBlob) );

    L->stack_.push( LuaValue(newBlob) );
  }
  return plib;
}


/*
** __gc tag method: calls library's `ll_unloadlib' function with the lib
** handle
*/
static int gctm (LuaThread *L) {
  THREAD_CHECK(L);
  void **lib = (void **)luaL_checkudata(L, 1, "_LOADLIB");
  if (*lib) ll_unloadlib(*lib);
  *lib = NULL;  /* mark library as closed */
  return 0;
}


static int ll_loadfunc (LuaThread *L, const char *path, const char *sym) {
  THREAD_CHECK(L);
  void **reg = ll_register(L, path);
  if (*reg == NULL) *reg = ll_load(L, path, *sym == '*');
  if (*reg == NULL) return ERRLIB;  /* unable to load library */
  if (*sym == '*') {  /* loading only library (no function)? */
    lua_pushboolean(L, 1);  /* return 'true' */
    return 0;  /* no errors */
  }
  else {
    LuaCallback f = ll_sym(L, *reg, sym);
    if (f == NULL)
      return ERRFUNC;  /* unable to find function */
    L->stack_.push(f);
    return 0;  /* no errors */
  }
}


static int ll_loadlib (LuaThread *L) {
  THREAD_CHECK(L);
  const char *path = luaL_checkstring(L, 1);
  const char *init = luaL_checkstring(L, 2);
  int stat = ll_loadfunc(L, path, init);
  if (stat == 0)  /* no errors? */
    return 1;  /* return the loaded function */
  else {  /* error; error message is on stack top */
    L->stack_.push(LuaValue::Nil());
    lua_insert(L, -2);
    lua_pushstring(L, (stat == ERRLIB) ?  LIB_FAIL : "init");
    return 3;  /* return nil, error message, and where */
  }
}



/*
** {======================================================
** 'require' function
** =======================================================
*/


static int readable (const char *filename) {
  FILE *f = fopen(filename, "r");  /* try to open file */
  if (f == NULL) return 0;  /* open failed */
  fclose(f);
  return 1;
}


static const char *pushnexttemplate (LuaThread *L, const char *path) {
  THREAD_CHECK(L);
  const char *l;
  while (*path == *LUA_PATH_SEP) path++;  /* skip separators */
  if (*path == '\0') return NULL;  /* no more templates */
  l = strchr(path, *LUA_PATH_SEP);  /* find next separator */
  if (l == NULL) l = path + strlen(path);
  lua_pushlstring(L, path, l - path);  /* template */
  return l;
}


static const char *searchpath (LuaThread *L, const char *name,
                                             const char *path,
                                             const char *sep,
                                             const char *dirsep) {
  THREAD_CHECK(L);
  luaL_Buffer msg;  /* to build error message */
  luaL_buffinit(L, &msg);
  if (*sep != '\0')  /* non-empty separator? */
    name = luaL_gsub(L, name, sep, dirsep);  /* replace it by 'dirsep' */
  while ((path = pushnexttemplate(L, path)) != NULL) {
    const char *filename = luaL_gsub(L, lua_tostring(L, -1),
                                     LUA_PATH_MARK, name);
    L->stack_.remove(-2);  /* remove path template */
    if (readable(filename)) {
      /* does file exist and is readable? */
      return filename;  /* return that file name */
    }
    lua_pushfstring(L, "\n\tno file " LUA_QS, filename);
    L->stack_.remove(-2);  /* remove file name */
    luaL_addvalue(&msg);  /* concatenate error msg. entry */
  }
  luaL_pushresult(&msg);  /* create error message */
  return NULL;  /* not found */
}


static int ll_searchpath (LuaThread *L) {
  THREAD_CHECK(L);
  const char *f = searchpath(L, luaL_checkstring(L, 1),
                                luaL_checkstring(L, 2),
                                luaL_optstring(L, 3, "."),
                                luaL_optstring(L, 4, LUA_DIRSEP));
  if (f != NULL) return 1;
  else {  /* error message is on top of the stack */
    L->stack_.push(LuaValue::Nil());
    lua_insert(L, -2);
    return 2;  /* return nil + error message */
  }
}


static const char *findfile (LuaThread *L, const char *name,
                                           const char *pname,
                                           const char *dirsep) {
  THREAD_CHECK(L);
  const char *path;
  lua_getfield(L, lua_upvalueindex(1), pname);
  path = lua_tostring(L, -1);
  if (path == NULL)
    luaL_error(L, LUA_QL("package.%s") " must be a string", pname);
  return searchpath(L, name, path, ".", dirsep);
}


static int checkload (LuaThread *L, int stat, const char *filename) {
  THREAD_CHECK(L);
  if (stat) {  /* module loaded successfully? */
    lua_pushstring(L, filename);  /* will be 2nd argument to module */
    return 2;  /* return open function and file name */
  }
  else
    return luaL_error(L, "error loading module " LUA_QS
                         " from file " LUA_QS ":\n\t%s",
                          lua_tostring(L, 1), filename, lua_tostring(L, -1));
}


static int searcher_Lua (LuaThread *L) {
  THREAD_CHECK(L);
  const char *filename;
  const char *name = luaL_checkstring(L, 1);
  filename = findfile(L, name, "path", LUA_LSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (luaL_loadfile(L, filename) == LUA_OK), filename);
}


static int loadfunc (LuaThread *L, const char *filename, const char *modname) {
  THREAD_CHECK(L);
  const char *funcname;
  const char *mark;
  modname = luaL_gsub(L, modname, ".", LUA_OFSEP);
  mark = strchr(modname, *LUA_IGMARK);
  if (mark) {
    int stat;
    funcname = lua_pushlstring(L, modname, mark - modname);
    funcname = lua_pushfstring(L, POF"%s", funcname);
    stat = ll_loadfunc(L, filename, funcname);
    if (stat != ERRFUNC) return stat;
    modname = mark + 1;  /* else go ahead and try old-style name */
  }
  funcname = lua_pushfstring(L, POF"%s", modname);
  return ll_loadfunc(L, filename, funcname);
}


static int searcher_C (LuaThread *L) {
  THREAD_CHECK(L);
  const char *name = luaL_checkstring(L, 1);
  const char *filename = findfile(L, name, "cpath", LUA_CSUBSEP);
  if (filename == NULL) return 1;  /* module not found in this path */
  return checkload(L, (loadfunc(L, filename, name) == 0), filename);
}


static int searcher_Croot (LuaThread *L) {
  THREAD_CHECK(L);
  const char *filename;
  const char *name = luaL_checkstring(L, 1);
  const char *p = strchr(name, '.');
  int stat;
  if (p == NULL) return 0;  /* is root */
  lua_pushlstring(L, name, p - name);
  filename = findfile(L, lua_tostring(L, -1), "cpath", LUA_CSUBSEP);
  if (filename == NULL) return 1;  /* root not found */
  if ((stat = loadfunc(L, filename, name)) != 0) {
    if (stat != ERRFUNC)
      return checkload(L, 0, filename);  /* real error */
    else {  /* open function not found */
      lua_pushfstring(L, "\n\tno module " LUA_QS " in file " LUA_QS,
                         name, filename);
      return 1;
    }
  }
  lua_pushstring(L, filename);  /* will be 2nd argument to module */
  return 2;
}


static int searcher_preload (LuaThread *L) {
  THREAD_CHECK(L);
  const char *name = luaL_checkstring(L, 1);
  lua_getregistryfield(L, "_PRELOAD");
  lua_getfield(L, -1, name);
  if (lua_isnil(L, -1)) {
    /* not found? */
    lua_pushfstring(L, "\n\tno field package.preload['%s']", name);
  }
  return 1;
}


static void findloader (LuaThread *L, const char *name) {
  THREAD_CHECK(L);
  int i;
  luaL_Buffer msg;  /* to build error message */
  luaL_buffinit(L, &msg);
  lua_getfield(L, lua_upvalueindex(1), "searchers");  /* will be at index 3 */
  if (!lua_istable(L, 3))
    luaL_error(L, LUA_QL("package.searchers") " must be a table");
  /*  iterate over available seachers to find a loader */
  for (i = 1; ; i++) {
    lua_rawgeti(L, 3, i);  /* get a seacher */
    if (lua_isnil(L, -1)) {  /* no more searchers? */
      L->stack_.pop();  /* remove nil */
      luaL_pushresult(&msg);  /* create error message */
      luaL_error(L, "module " LUA_QS " not found:%s",
                    name, lua_tostring(L, -1));
    }
    lua_pushstring(L, name);
    lua_call(L, 1, 2);  /* call it */
    if (lua_isfunction(L, -2))  /* did it find a loader? */
      return;  /* module loader found */
    else if (lua_isStringable(L, -2)) {  /* searcher returned error message? */
      L->stack_.pop();  /* remove extra return */
      luaL_addvalue(&msg);  /* concatenate error message */
    }
    else
      L->stack_.pop(2);  /* remove both returns */
  }
}


static int ll_require (LuaThread *L) {
  THREAD_CHECK(L);
  const char *name = luaL_checkstring(L, 1);
  L->stack_.setTopIndex(1);  /* _LOADED table will be at index 2 */
  lua_getregistryfield(L, "_LOADED");
  lua_getfield(L, 2, name);  /* _LOADED[name] */
  if (lua_toboolean(L, -1))  /* is it there? */
    return 1;  /* package is already loaded */
  /* else must load package */
  L->stack_.pop();  /* remove 'getfield' result */
  findloader(L, name);
  lua_pushstring(L, name);  /* pass name as argument to module loader */
  lua_insert(L, -2);  /* name is 1st argument (before search data) */
  lua_call(L, 2, 1);  /* run loader to load module */
  if (!lua_isnil(L, -1))  /* non-nil return? */
    lua_setfield(L, 2, name);  /* _LOADED[name] = returned value */
  lua_getfield(L, 2, name);
  if (lua_isnil(L, -1)) {   /* module did not set a value? */
    lua_pushboolean(L, 1);  /* use true as result */
    L->stack_.copy(-1);  /* extra copy to be returned */
    lua_setfield(L, 2, name);  /* _LOADED[name] = true */
  }
  return 1;
}

/* }====================================================== */



/* auxiliary mark (for internal use) */
#define AUXMARK		"\1"


int luaopen_package (LuaThread *L) {
  THREAD_CHECK(L);

  /* create new type _LOADLIB */
  LuaTable* meta = new LuaTable();
  meta->set("__gc", gctm);
  L->l_G->getRegistry()->set("_LOADLIB", meta);


  /* create `package' table */
  LuaTable* package = new LuaTable(0, 2);
  package->set("loadlib", ll_loadlib);
  package->set("searchpath", ll_searchpath);

  L->stack_.push( LuaValue(package) );

  /* create 'searchers' table */
  LuaTable* search = new LuaTable(4, 0);
  search->set( 1, new LuaClosure(searcher_preload,package) );
  search->set( 2, new LuaClosure(searcher_Lua,package) );
  search->set( 3, new LuaClosure(searcher_C,package) );
  search->set( 4, new LuaClosure(searcher_Croot,package) );

  /* put it in field 'searchers' */
  package->set("searchers", search);

  /* set field 'path' */

  std::string dir = GetModuleDirectory();

  std::string path = replace_all(LUA_PATH_DEFAULT, LUA_EXEC_DIR, dir.c_str());
  std::string cpath = replace_all(LUA_CPATH_DEFAULT, LUA_EXEC_DIR, dir.c_str());

  LuaString* path2 = L->l_G->strings_->Create(path.c_str());
  LuaString* cpath2 = L->l_G->strings_->Create(cpath.c_str());

  package->set("path", path2);
  package->set("cpath", cpath2);

  LuaString* config = L->l_G->strings_->Create(LUA_DIRSEP "\n" LUA_PATH_SEP "\n" LUA_PATH_MARK "\n" LUA_EXEC_DIR "\n" LUA_IGMARK "\n");
  package->set("config", config);

  LuaTable* loadedModules = L->l_G->getRegistryTable("_LOADED");
  LuaTable* preloadedModules = L->l_G->getRegistryTable("_PRELOAD");

  package->set("loaded", loadedModules );
  package->set("preload", preloadedModules );

  // put 'require' in the globals table
  LuaTable* globals = L->l_G->getGlobals();
  globals->set("require", new LuaClosure(ll_require,package));

  loadedModules->set("package", package);
  globals->set("package", package);

  return 1;  /* return 'package' table */
}

