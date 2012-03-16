/*
** $Id: lobject.h,v 2.64 2011/10/31 17:48:22 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"

#include "LuaBase.h"
class Table;



#include "LuaValue.h"


typedef TValue* StkId;  /* index to stack elements */

#include "LuaString.h"


/*
** Header for userdata; memory area follows the end of this structure
*/
__declspec(align(8)) struct Udata : public LuaBase {
  Table *metatable;
  Table *env;
  size_t len;  /* number of bytes */
};



/*
** Description of an upvalue for function prototypes
*/
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  uint8_t instack;  /* whether it is in stack */
  uint8_t idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
struct Proto : public LuaBase {
  TValue *constants;  /* constants used by the function */
  int nconstants;  /* size of `constants' */

  Instruction *code;
  Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  Closure *cache;  /* last created closure with this prototype */
  TString  *source;  /* used for debug information */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of `p' */
  int sizelocvars;
  int linedefined;
  int lastlinedefined;
  LuaBase *gclist;
  uint8_t numparams;  /* number of fixed parameters */
  uint8_t is_vararg;
  uint8_t maxstacksize;  /* maximum stack used by this function */
};



/*
** Lua Upvalues
*/
class UpVal : public LuaBase {
public:
  TValue *v;  /* points to stack or to its own value */

  struct {
    TValue value;  // the value (when closed)
    struct {  // double linked list (when open)
      UpVal *prev;
      UpVal *next;
    } l;
  } u;

  char pad[128];

  /*
  TValue value;  // the value (when closed)
  UpVal *prev;
  UpVal *next;
  */
};


/*
** Closures
*/


class Closure : public LuaBase {
public:
  uint8_t isC;
  uint8_t nupvalues;
  LuaBase *gclist;

  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */

  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
};


#define isLfunction(o)	ttisLclosure(o)

#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

#include "LuaTable.h"



/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1ull<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
*/
#define luaO_nilobject		(&luaO_nilobject_)


extern const TValue luaO_nilobject_;


int luaO_int2fb (unsigned int x);
int luaO_fb2int (int x);
int luaO_ceillog2 (unsigned int x);
lua_Number luaO_arith (int op, lua_Number v1, lua_Number v2);
int luaO_str2d (const char *s, size_t len, lua_Number *result);
int luaO_hexavalue (int c);
const char *luaO_pushvfstring (lua_State *L, const char *fmt, va_list argp);
const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
void luaO_chunkid (char *out, const char *source, size_t len);


#endif

