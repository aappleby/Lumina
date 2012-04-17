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
typedef uint32_t Instruction;


//-----------------------------------------------------------------------------

enum LuaResult
{
  LR_OK = 0,
  LR_BAD_TABLE,
  LR_BAD_INDEX_TM,
  LR_META_LOOP,
  LR_BAD_MATH,  // tried to do math on things that weren't numbers
};

// Whether 'val' points to a value on the Lua stack or elsewhere _does_ matter,
// as it's used to deduce what sort of variable 'val' is.
void handleError(LuaResult err, const TValue* val);

//-----------------------------------------------------------------------------

void  luaM_free(void * blob);

LuaObject** getGlobalGCHead();

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
enum LuaType {
  LUA_TNIL           = 0,   // Nil - valid value, but contains nothing.
  LUA_TBOOLEAN       = 1,   // Boolean
  LUA_TLIGHTUSERDATA = 2,   // User-supplied void*
  LUA_TNUMBER        = 3,   // Double-precision floating point number
  LUA_TSTRING        = 4,   // String
  LUA_TTABLE         = 5,   // Table
  LUA_TLCL           = 6,   // Lua closure
  LUA_TUSERDATA      = 7,   // User-supplied blob of bytes
  LUA_TTHREAD        = 8,   // One execution state, like a thread.
  LUA_TLCF           = 9,   // C function pointer, used like a callback.
  LUA_TCCL           = 10,  // C closure - function pointer with persistent state
  LUA_TPROTO         = 11,  // Function prototype, contains VM opcodes
  LUA_TUPVAL         = 12,  // Persistent state object for C and Lua closuers
  LUA_TNONE          = 13,  // None - invalid value, what you get if you read past the end of an array.
  LUA_NUMTAGS        = 14,
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



