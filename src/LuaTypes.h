#pragma once
#include "stdint.h"

class LuaObject;
class TString;
class lua_State;
class lua_Debug;
class global_State;
class Closure;
class stringtable;
class CallInfo;
class TValue;
class UpVal;
class Table;

typedef TValue* StkId;  /* index to stack elements */

typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);
typedef int (*lua_CFunction) (lua_State *L);

typedef double lua_Number;
typedef ptrdiff_t lua_Integer;
typedef uint32_t lua_Unsigned;
typedef ptrdiff_t l_mem;
typedef uint32_t Instruction;

//-----------------------------------------------------------------------------

enum LuaAllocPool {
  LAP_STARTUP,
  LAP_RUNTIME,
  LAP_OBJECT,
  LAP_VECTOR,
};

void* luaM_alloc(size_t size, int pool);
void  luaM_free(void * blob);

void  luaM_delobject(void * blob);

//-----------------------------------------------------------------------------

extern __declspec(thread) lua_State* thread_L;
extern __declspec(thread) global_State* thread_G;

class LuaScope {
public:
  LuaScope(lua_State* L);
  ~LuaScope();

  lua_State* oldState;
};

class LuaGlobalScope {
public:
  LuaGlobalScope(lua_State* L);
  ~LuaGlobalScope();

  lua_State* oldState;
};

#define THREAD_CHECK(A)  assert((thread_L == A) && (thread_G == A->l_G));
#define THREAD_CHANGE(A) LuaScope luascope(A);
#define GLOBAL_CHANGE(A) LuaGlobalScope luascope(A);

//-----------------------------------------------------------------------------


/*
** basic types
*/
enum LuaTag {
  LUA_TNONE          = -1,
  LUA_TNIL           = 0,
  LUA_TBOOLEAN       = 1,
  LUA_TLIGHTUSERDATA = 2,
  LUA_TNUMBER        = 3,
  LUA_TSTRING        = 4,
  LUA_TTABLE         = 5,
  LUA_TFUNCTION      = 6,
  LUA_TUSERDATA      = 7,
  LUA_TTHREAD        = 8,
  LUA_NUMTAGS        = 9,

  // non-values
  LUA_TPROTO = 9,
  LUA_TUPVAL = 10,
  LUA_TDEADKEY = 11,

  LUA_ALLTAGS = LUA_TDEADKEY+1,
};


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TUPVAL+2)

/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER TM"
*/
typedef enum {
  TM_INDEX,
  TM_NEWINDEX,
  TM_GC,
  TM_MODE,
  TM_LEN,
  TM_EQ,  /* last tag method with `fast' access */
  TM_ADD,
  TM_SUB,
  TM_MUL,
  TM_DIV,
  TM_MOD,
  TM_POW,
  TM_UNM,
  TM_LT,
  TM_LE,
  TM_CONCAT,
  TM_CALL,
  TM_N		/* number of elements in the enum */
} TMS;



