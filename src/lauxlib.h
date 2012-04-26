/*
** $Id: lauxlib.h,v 1.120 2011/11/29 15:55:08 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/


#ifndef lauxlib_h
#define lauxlib_h


#include <stddef.h>
#include <stdio.h>

#include "lua.h"



/* extra error code for `luaL_load' */
#define LUA_ERRFILE     (LUA_ERRERR+1)


typedef struct luaL_Reg {
  const char *name;
  LuaCallback func;
} luaL_Reg;


void (luaL_checkversion_) (LuaThread *L, double ver);
#define luaL_checkversion(L)	luaL_checkversion_(L, LUA_VERSION_NUM)

int (luaL_getmetafield) (LuaThread *L, int obj, const char *e);
int (luaL_callmeta) (LuaThread *L, int obj, const char *e);
const char *(luaL_tolstring) (LuaThread *L, int idx, size_t *len);
int (luaL_argerror) (LuaThread *L, int numarg, const char *extramsg);
const char *(luaL_checklstring) (LuaThread *L, int numArg,
                                                          size_t *l);
const char *(luaL_optlstring) (LuaThread *L, int numArg,
                                          const char *def, size_t *l);
double (luaL_checknumber) (LuaThread *L, int numArg);
double (luaL_optnumber) (LuaThread *L, int nArg, double def);

ptrdiff_t (luaL_checkinteger) (LuaThread *L, int numArg);
ptrdiff_t (luaL_optinteger) (LuaThread *L, int nArg,
                                          ptrdiff_t def);
uint32_t (luaL_checkunsigned) (LuaThread *L, int numArg);
uint32_t (luaL_optunsigned) (LuaThread *L, int numArg,
                                            uint32_t def);

void (luaL_checkstack) (LuaThread *L, int sz, const char *msg);
void (luaL_checktype) (LuaThread *L, int narg, int t);
void (luaL_checkany) (LuaThread *L, int narg);

void luaL_checkIsFunction (LuaThread *L, int narg);
void luaL_checkIsTable    (LuaThread *L, int narg);

int   (luaL_newmetatable) (LuaThread *L, const char *tname);
void  (luaL_setmetatable) (LuaThread *L, const char *tname);
void *(luaL_testudata) (LuaThread *L, int ud, const char *tname);
void *(luaL_checkudata) (LuaThread *L, int ud, const char *tname);

void (luaL_where) (LuaThread *L, int lvl);
int (luaL_error) (LuaThread *L, const char *fmt, ...);

int (luaL_checkoption) (LuaThread *L, int narg, const char *def,
                                   const char *const lst[]);

int (luaL_fileresult) (LuaThread *L, int stat, const char *fname);
int (luaL_execresult) (LuaThread *L, int stat);

/* pre-defined references */
#define LUA_NOREF       (-2)
#define LUA_REFNIL      (-1)

int (luaL_ref) (LuaThread *L, int t);
void (luaL_unref) (LuaThread *L, int t, int ref);

int (luaL_loadfilex) (LuaThread *L, const char *filename,
                                               const char *mode);

#define luaL_loadfile(L,f)	luaL_loadfilex(L,f,NULL)

int (luaL_loadbufferx) (LuaThread *L, const char *buff, size_t sz,
                                   const char *name, const char *mode);
int (luaL_loadstring) (LuaThread *L, const char *s);

LuaThread *(luaL_newstate) (void);

int (luaL_len) (LuaThread *L, int idx);

const char *(luaL_gsub) (LuaThread *L, const char *s, const char *p,
                                                  const char *r);

void (luaL_setfuncs) (LuaThread *L, const luaL_Reg *l, int nup);

int (luaL_getsubtable) (LuaThread *L, int idx, const char *fname);

void (luaL_traceback) (LuaThread *L, LuaThread *L1,
                                  const char *msg, int level);

void (luaL_requiref) (LuaThread *L, const char *modname,
                                 LuaCallback openf, int glb);

/*
** ===============================================================
** some useful macros
** ===============================================================
*/


/*

#define luaL_newlibtable(L,l)	\
  lua_createtable(L, 0, sizeof(l)/sizeof((l)[0]) - 1)

#define luaL_newlib(L,l)	(luaL_newlibtable(L,l), luaL_setfuncs(L,l,0))

*/


#define luaL_argcheck(L, cond,numarg,extramsg) ((void)((cond) || luaL_argerror(L, (numarg), (extramsg))))
#define luaL_checkstring(L,n)	(luaL_checklstring(L, (n), NULL))
#define luaL_optstring(L,n,d)	(luaL_optlstring(L, (n), (d), NULL))
#define luaL_checkint(L,n)	((int)luaL_checkinteger(L, (n)))
#define luaL_optint(L,n,d)	((int)luaL_optinteger(L, (n), (d)))
#define luaL_checklong(L,n)	((long)luaL_checkinteger(L, (n)))
#define luaL_optlong(L,n,d)	((long)luaL_optinteger(L, (n), (d)))

const char* luaL_typename(LuaThread* L, int index);

void luaL_typecheck(LuaThread* L, int index, LuaType a);
void luaL_typecheck(LuaThread* L, int index, LuaType a, LuaType b);
void luaL_typecheck(LuaThread* L, int index, LuaType a, LuaType b, LuaType c);

#define luaL_dofile(L, fn) \
	(luaL_loadfile(L, fn) || lua_pcall(L, 0, LUA_MULTRET, 0))

#define luaL_dostring(L, s) \
	(luaL_loadstring(L, s) || lua_pcall(L, 0, LUA_MULTRET, 0))

void luaL_getmetatable(LuaThread* L, const char* tname);

#define luaL_opt(L,f,n,d)	(lua_isnoneornil(L,(n)) ? (d) : f(L,(n)))

#define luaL_loadbuffer(L,s,sz,n)	luaL_loadbufferx(L,s,sz,n,NULL)


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

typedef struct luaL_Buffer {
  char *b;  /* buffer address */
  size_t size;  /* buffer size */
  size_t n;  /* number of characters in buffer */
  LuaThread *L;
  char initb[LUAL_BUFFERSIZE];  /* initial buffer */
} luaL_Buffer;


#define luaL_addchar(B,c) \
  ((void)((B)->n < (B)->size || luaL_prepbuffsize((B), 1)), \
   ((B)->b[(B)->n++] = (c)))

#define luaL_addsize(B,s)	((B)->n += (s))

void (luaL_buffinit) (LuaThread *L, luaL_Buffer *B);
char *(luaL_prepbuffsize) (luaL_Buffer *B, size_t sz);
void (luaL_addlstring) (luaL_Buffer *B, const char *s, size_t l);
void (luaL_addstring) (luaL_Buffer *B, const char *s);
void (luaL_addvalue) (luaL_Buffer *B);
void (luaL_pushresult) (luaL_Buffer *B);
void (luaL_pushresultsize) (luaL_Buffer *B, size_t sz);
char *(luaL_buffinitsize) (LuaThread *L, luaL_Buffer *B, size_t sz);

#define luaL_prepbuffer(B)	luaL_prepbuffsize(B, LUAL_BUFFERSIZE)

/* }====================================================== */



/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'LUA_FILEHANDLE' and
** initial structure 'luaL_Stream' (it may contain other fields
** after that initial structure).
*/

#define LUA_FILEHANDLE          "FILE*"


typedef struct luaL_Stream {
  FILE *f;  /* stream (NULL for incompletely created streams) */
  LuaCallback closef;  /* to close stream (NULL for closed streams) */
} luaL_Stream;

/* }====================================================== */



/* compatibility with old module system */
#if defined(LUA_COMPAT_MODULE)

void (luaL_pushmodule) (LuaThread *L, const char *modname,
                                   int sizehint);
void (luaL_openlib) (LuaThread *L, const char *libname,
                                const luaL_Reg *l, int nup);

#define luaL_register(L,n,l)	(luaL_openlib(L,(n),(l),0))

#endif


#endif


