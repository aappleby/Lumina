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
class Proto;
class Udata;

typedef TValue* StkId;  /* index to stack elements */

typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);
typedef int (*lua_CFunction) (lua_State *L);

typedef double lua_Number;
typedef ptrdiff_t lua_Integer;
typedef uint32_t lua_Unsigned;
typedef ptrdiff_t l_mem;
typedef uint32_t Instruction;

//-----------------------------------------------------------------------------

void* luaM_alloc(size_t size);
void  luaM_free(void * blob);

LuaObject*& getGlobalGCHead();

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
  LUA_TLCL           = 6,   /* Lua closure */
  LUA_TUSERDATA      = 7,
  LUA_TTHREAD        = 8,
  LUA_TLCF           = 9,  /* light C function */
  LUA_TCCL           = 10,  /* C closure */

  LUA_NUMTAGS        = 11,

  // non-values
  LUA_TPROTO = 12,
  LUA_TUPVAL = 13,
  LUA_TDEADKEY = 14,
};

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



