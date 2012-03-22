#pragma once

#include "LuaObject.h"
#include "LuaVector.h"

/*
** Description of an upvalue for function prototypes
*/
struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  uint8_t instack;  /* whether it is in stack */
  uint8_t idx;  /* index of upvalue (in stack or in outer function's list) */
};


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
};


/*
** Function Prototypes
*/
class Proto : public LuaObject {
public:
  Proto();
  LuaVector<TValue> constants;
  LuaVector<Instruction> code;
  LuaVector<int> lineinfo;
  LuaVector<Proto*> p; // functions defined inside the function
  LuaVector<LocVar> locvars; // information about local variables (debug information)
  LuaVector<Upvaldesc> upvalues; // upvalue information
  Closure *cache;  /* last created closure with this prototype */
  TString  *source;  /* used for debug information */
  int linedefined;
  int lastlinedefined;
  LuaObject *gclist;
  uint8_t numparams;  /* number of fixed parameters */
  uint8_t is_vararg;
  uint8_t maxstacksize;  /* maximum stack used by this function */
};

