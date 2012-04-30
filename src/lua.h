/*
** $Id: lua.h,v 1.282 2011/11/29 15:55:08 roberto Exp $
** Lua - A Scripting Language
** Lua.org, PUC-Rio, Brazil (http://www.lua.org)
** See Copyright Notice at the end of this file
*/


#ifndef lua_h
#define lua_h

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#include "stdint.h"
#include <string>

#include "LuaDefines.h"

#include "luaconf.h"

#include "LuaTypes.h"

LuaValue* index2addr (LuaThread *L, int idx);
LuaValue* index2addr2 (LuaThread *L, int idx);

/*
** functions that read/write blocks when loading/dumping Lua chunks
*/
typedef const char * (*lua_Reader) (LuaThread *L, void *ud, size_t *sz);

typedef int (*lua_Writer) (LuaThread *L, const void* p, size_t sz, void* ud);


/*
** prototype for memory-allocation functions
*/
typedef void * (*lua_Alloc) (void *ptr, size_t osize, size_t nsize);








#include "LuaValue.h"



/*
** state manipulation
*/
LuaThread *(lua_newstate) ();
void       (lua_close) (LuaThread *L);
LuaThread *(lua_newthread) (LuaThread *L);

LuaCallback (lua_atpanic) (LuaThread *L, LuaCallback panicf);


const double *(lua_version) (LuaThread *L);


/*
** basic stack manipulation
*/
int   (lua_absindex) (LuaThread *L, int idx);
void  (lua_insert) (LuaThread *L, int idx);
void  (lua_replace) (LuaThread *L, int idx);
void  (lua_copy) (LuaThread *L, int fromidx, int toidx);
int   (lua_checkstack) (LuaThread *L, int sz);

void  (lua_xmove) (LuaThread *from, LuaThread *to, int n);


/*
** access functions (stack -> C)
*/

int             (lua_isNumberable) (LuaThread *L, int idx);
int             (lua_isStringable) (LuaThread *L, int idx);

int             (lua_iscfunction) (LuaThread *L, int idx);
int             (lua_isuserdata) (LuaThread *L, int idx);
int             (lua_type) (LuaThread *L, int idx);
const char     *(lua_typename) (LuaThread *L, int tp);

double      (lua_tonumberx) (LuaThread *L, int idx, int *isnum);
ptrdiff_t     (lua_tointegerx) (LuaThread *L, int idx, int *isnum);
uint32_t    (lua_tounsignedx) (LuaThread *L, int idx, int *isnum);
int             (lua_toboolean) (LuaThread *L, int idx);
const char     *(lua_tolstring) (LuaThread *L, int idx, size_t *len);
size_t          (lua_rawlen) (LuaThread *L, int idx);
LuaCallback   (lua_tocfunction) (LuaThread *L, int idx);
void	       *(lua_touserdata) (LuaThread *L, int idx);
LuaThread      *(lua_tothread) (LuaThread *L, int idx);
const void     *(lua_topointer) (LuaThread *L, int idx);


/*
** Comparison and arithmetic functions
*/

#define LUA_OPADD	0	/* ORDER TM */
#define LUA_OPSUB	1
#define LUA_OPMUL	2
#define LUA_OPDIV	3
#define LUA_OPMOD	4
#define LUA_OPPOW	5
#define LUA_OPUNM	6

void  (lua_arith) (LuaThread *L, int op);

#define LUA_OPEQ	0
#define LUA_OPLT	1
#define LUA_OPLE	2

int   (lua_rawequal) (LuaThread *L, int idx1, int idx2);
int   (lua_compare) (LuaThread *L, int idx1, int idx2, int op);


/*
** push functions (C -> stack)
*/
void        (lua_pushnumber) (LuaThread *L, double n);
void        (lua_pushinteger) (LuaThread *L, ptrdiff_t n);
void        (lua_pushunsigned) (LuaThread *L, uint32_t n);
const char *(lua_pushlstring) (LuaThread *L, const char *s, size_t l);
const char *(lua_pushstring) (LuaThread *L, const char *s);
const char *(lua_pushvfstring) (LuaThread *L, const char *fmt,
                                                      va_list argp);
const char *(lua_pushfstring) (LuaThread *L, const char *fmt, ...);
void  (lua_pushcclosure) (LuaThread *L, LuaCallback fn, int n);
void  (lua_pushboolean) (LuaThread *L, int b);
void  (lua_pushlightuserdata) (LuaThread *L, void *p);
int   (lua_pushthread) (LuaThread *L);


/*
** get functions (Lua -> stack)
*/
void  (lua_getglobal) (LuaThread *L, const char *var);
void  (lua_gettable) (LuaThread *L, int idx);
void  (lua_getfield) (LuaThread *L, int idx, const char *k);
void  (lua_rawget) (LuaThread *L, int idx);
void  (lua_rawgeti) (LuaThread *L, int idx, int n);
void  (lua_rawgetp) (LuaThread *L, int idx, const void *p);
void  (lua_createtable) (LuaThread *L, int narr, int nrec);
void *(lua_newuserdata) (LuaThread *L, size_t sz);
int   (lua_getmetatable) (LuaThread *L, int objindex);

LuaTable* lua_getmetatable(LuaValue v);

void lua_getregistryfield(LuaThread* L, const char* field);
void lua_getglobalfield(LuaThread* L, const char* field);


/*
** set functions (stack -> Lua)
*/
void  (lua_setglobal) (LuaThread *L, const char *var);
void  (lua_settable) (LuaThread *L, int idx);
void  (lua_setfield) (LuaThread *L, int idx, const char *k);
void  (lua_rawset) (LuaThread *L, int idx);
void  (lua_rawseti) (LuaThread *L, int idx, int n);
void  (lua_rawsetp) (LuaThread *L, int idx, const void *p);
int   (lua_setmetatable) (LuaThread *L, int objindex);

void lua_setregistryfield(LuaThread *L, const char* field);
void lua_setglobalfield(LuaThread* L, const char* field);

/*
** 'load' and 'call' functions (load and run Lua code)
*/
void  (lua_callk) (LuaThread *L, int nargs, int nresults, int ctx, LuaCallback continuation);

//#define lua_call(L,n,r)		lua_callk(L, (n), (r), 0, NULL)

void lua_call(LuaThread* L, int nargs, int nresults);

int   (lua_getctx) (LuaThread *L, int *ctx);

int   (lua_pcallk) (LuaThread *L, int nargs, int nresults, int errfunc,
                            int ctx, LuaCallback k);

int   (lua_pcall) (LuaThread *L, int nargs, int nresults, int errfunc);
//#define lua_pcall(L,n,r,f)	lua_pcallk(L, (n), (r), (f), 0, NULL)

int   (lua_load) (LuaThread *L, lua_Reader reader, void *dt,
                                        const char *chunkname,
                                        const char *mode);

int (lua_dump) (LuaThread *L, lua_Writer writer, void *data);


/*
** coroutine functions
*/
int  (lua_yieldk) (LuaThread *L, int nresults, int ctx,
                           LuaCallback k);

int  (lua_yield)  (LuaThread *L, int nresults);

int  (lua_resume) (LuaThread *L, LuaThread *from, int narg);

/*
** garbage-collection function and options
*/

#define LUA_GCSTOP		0
#define LUA_GCRESTART		1
#define LUA_GCCOLLECT		2
#define LUA_GCCOUNT		3
#define LUA_GCCOUNTB		4
#define LUA_GCSTEP		5
#define LUA_GCSETPAUSE		6
#define LUA_GCSETSTEPMUL	7
#define LUA_GCSETMAJORINC	8
#define LUA_GCISRUNNING		9
#define LUA_GCGEN		10
#define LUA_GCINC		11

int (lua_gc) (LuaThread *L, int what, int data);


/*
** miscellaneous functions
*/

int   (lua_error) (LuaThread *L);

int   (lua_next) (LuaThread *L, int idx);

void  (lua_concat) (LuaThread *L, int n);
void  (lua_len)    (LuaThread *L, int idx);


/*
** ===============================================================
** some useful macros
** ===============================================================
*/

#define lua_tonumber(L,i)	lua_tonumberx(L,i,NULL)
#define lua_tointeger(L,i)	lua_tointegerx(L,i,NULL)
#define lua_tounsigned(L,i)	lua_tounsignedx(L,i,NULL)

/*
#define lua_newtable(L)		lua_createtable(L, 0, 0)
*/

#define lua_register(L,n,f) (lua_pushcfunction(L, (f)), lua_setglobal(L, (n)))

inline bool lua_isfunction(LuaThread* L, int n) {
  LuaValue* v = index2addr2(L,n);
  return v && v->isFunction();
}

#define lua_istable(L,n)	(lua_type(L, (n)) == LUA_TTABLE)
#define lua_islightuserdata(L,n)	(lua_type(L, (n)) == LUA_TPOINTER)
#define lua_isboolean(L,n)	(lua_type(L, (n)) == LUA_TBOOLEAN)
#define lua_isthread(L,n)	(lua_type(L, (n)) == LUA_TTHREAD)

inline bool lua_isnil(LuaThread* L, int n) {
  LuaValue* v = index2addr2(L, n);
  return v && v->isNil();
}

inline bool lua_isnone(LuaThread* L, int n) {
  LuaValue* v = index2addr2(L, n);
  return (v == NULL);
}

inline bool lua_isnoneornil(LuaThread* L, int n) {
  LuaValue* v = index2addr2(L, n);

  if(v == NULL) return true;
  if(v->isNil()) return true;
  return false;
}

inline void lua_pushliteral(LuaThread* L, const char* s) {
  lua_pushlstring(L, s, strlen(s) );
}

void lua_pushglobaltable(LuaThread* L);

#define lua_tostring(L,i)	lua_tolstring(L, (i), NULL)



/*
** {======================================================================
** Debug API
** =======================================================================
*/


/*
** Event codes
*/
#define LUA_HOOKCALL	0
#define LUA_HOOKRET	1
#define LUA_HOOKLINE	2
#define LUA_HOOKCOUNT	3
#define LUA_HOOKTAILCALL 4


/*
** Event masks
*/
#define LUA_MASKCALL	(1 << LUA_HOOKCALL)
#define LUA_MASKRET	(1 << LUA_HOOKRET)
#define LUA_MASKLINE	(1 << LUA_HOOKLINE)
#define LUA_MASKCOUNT	(1 << LUA_HOOKCOUNT)

class LuaDebug;  /* activation record */


/* Functions to be called by the debugger in specific events */


int (lua_getstack) (LuaThread *L, int level, LuaDebug *ar);
int (lua_getinfo) (LuaThread *L, const char *what, LuaDebug *ar);
const char *(lua_getlocal) (LuaThread *L, const LuaDebug *ar, int n);
const char *(lua_setlocal) (LuaThread *L, const LuaDebug *ar, int n);
const char *(lua_getupvalue) (LuaThread *L, int funcindex, int n);
const char *(lua_setupvalue) (LuaThread *L, int funcindex, int n);

void *(lua_upvalueid) (LuaThread *L, int fidx, int n);
void  (lua_upvaluejoin) (LuaThread *L, int fidx1, int n1,
                                               int fidx2, int n2);

int (lua_sethook) (LuaThread *L, LuaHook func, int mask, int count);
LuaHook (lua_gethook) (LuaThread *L);
int (lua_gethookmask) (LuaThread *L);
int (lua_gethookcount) (LuaThread *L);

class LuaStackFrame;

class LuaDebug {
public:

  LuaDebug() {}

  LuaDebug(int event2, int line2, LuaStackFrame* ci2) {
    event = event2;
    currentline = line2;
    i_ci = ci2;
  }

  int event;
  
  //const char *name;	/* (n) */
  std::string name2;

  //const char *namewhat;	/* (n) 'global', 'local', 'field', 'method' */
  std::string namewhat2;

  //const char *what;	/* (S) 'Lua', 'C', 'main', 'tail' */
  std::string what2;

  //const char *source;	/* (S) */
  // TODO(aappleby): This could be wasting a bit of memory (making an
  // additional copy of the source), but since it's only in the debug tool
  // I don't think it matters.
  std::string source2;
  
  int currentline;	/* (l) */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  int nups;	/* (u) number of upvalues */
  int nparams;/* (u) number of parameters */
  bool isvararg;        /* (u) */
  bool istailcall;	/* (t) */
  
  //char short_src[LUA_IDSIZE]; /* (S) */
  std::string short_src2;

  /* private part */
  LuaStackFrame *i_ci;  /* active function */
};

/* }====================================================================== */


/******************************************************************************
* Copyright (C) 1994-2011 Lua.org, PUC-Rio.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/


#endif
