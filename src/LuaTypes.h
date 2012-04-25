#pragma once
#include "stdint.h"

class LuaObject;
class LuaString;
class LuaThread;
class LuaDebug;
class LuaVM;
class LuaClosure;
class LuaCollector;
class LuaStringTable;
class LuaStackFrame;
class LuaValue;
class LuaUpvalue;
class LuaTable;
class LuaProto;
class LuaBlob;
class LuaGCVisitor;

typedef LuaValue* StkId;  /* index to stack elements */

typedef void (*LuaHook) (LuaThread *L, LuaDebug *ar);
typedef int (*LuaCallback) (LuaThread *L);

typedef uint32_t Instruction;

//-----------------------------------------------------------------------------

void  luaM_free(void * blob);

LuaObject** getGlobalGCHead();

//-----------------------------------------------------------------------------

extern __declspec(thread) LuaThread* thread_L;
extern __declspec(thread) LuaVM* thread_G;

class LuaScope {
public:
  LuaScope(LuaThread* L);
  ~LuaScope();

  LuaThread* oldState;
};

class LuaGlobalScope {
public:
  LuaGlobalScope(LuaThread* L);
  ~LuaGlobalScope();

  LuaThread* oldState;
};

#define THREAD_CHECK(A)  assert((thread_L == A) && (thread_G == A->l_G));
#define THREAD_CHANGE(A) LuaScope luascope(A);
#define GLOBAL_CHANGE(A) LuaGlobalScope luascope(A);

//-----------------------------------------------------------------------------


/*
** basic types
*/
enum LuaType {
  LUA_TNIL      = 0,   // Nil - valid value, but contains nothing.
  LUA_TNONE     = 1,   // None - invalid value, what you get if you read past the end of an array.
  LUA_TBOOLEAN  = 2,   // Boolean
  LUA_TNUMBER   = 3,   // Double-precision floating point number
  LUA_TVOID     = 4,   // User-supplied void*
  LUA_TCALLBACK = 5,   // C function pointer, used like a callback.

  LUA_TSTRING   = 6,   // String
  LUA_TTABLE    = 7,   // LuaTable
  LUA_TTHREAD   = 8,   // One execution state, like a thread.
  LUA_TPROTO    = 9,   // Function prototype, contains VM opcodes
  LUA_TLCL      = 10,  // Lua closure
  LUA_TCCL      = 11,  // C closure - function pointer with persistent state
  LUA_TUPVALUE  = 12,  // Persistent state object for C and Lua closuers
  LUA_TBLOB     = 13,  // User-supplied blob of bytes

  LUA_NUMTAGS   = 14,
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

//-----------------------------------------------------------------------------

/* thread status */
enum LuaResult {
  LUA_OK		       = 0,
  LUA_YIELD	       = 1,
  LUA_ERRRUN	     = 2,
  LUA_ERRSYNTAX	   = 3,
  LUA_ERRMEM	     = 4,
  LUA_ERRGCMM	     = 5,
  LUA_ERRERR	     = 6,
  LUA_ERRSTACK     = 7,
  LUA_ERRKEY       = 8,
  LUA_BAD_TABLE    = 9,
  LUA_BAD_INDEX_TM = 10,
  LUA_META_LOOP    = 11,
  LUA_BAD_MATH     = 12,
};

/*
enum LuaResult
{
  LR_OK = 0,
  LR_BAD_TABLE,
  LR_BAD_INDEX_TM,
  LR_META_LOOP,
  LR_BAD_MATH,  // tried to do math on things that weren't numbers
};
*/

// Whether 'val' points to a value on the Lua stack or elsewhere _does_ matter,
// as it's used to deduce what sort of variable 'val' is.
void handleResult(LuaResult err, const LuaValue* val = NULL);
void throwError(LuaResult error);




