/*
** $Id: ltm.h,v 2.11 2011/02/28 17:32:10 roberto Exp $
** Tag methods
** See Copyright Notice in lua.h
*/

#ifndef ltm_h
#define ltm_h


#include "lobject.h"
#include "LuaTypes.h"

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



const char* ttypename(int tag);
const char* objtypename(const TValue* v);

const TValue *luaT_gettm (Table *events, TMS event, TString *ename);
const TValue *luaT_gettmbyobj (const TValue *o, TMS event);
void luaT_init ();

const TValue* fasttm  ( Table* table, TMS tag);

#endif
