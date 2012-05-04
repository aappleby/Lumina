/*
** $Id: ldebug.h,v 2.7 2011/10/07 20:45:19 roberto Exp $
** Auxiliary functions from Debug Interface module
** See Copyright Notice in lua.h
*/

#ifndef ldebug_h
#define ldebug_h


#include "lstate.h"
#include "LuaProto.h"


//#define pcRel(pc, p)	(cast(int, (pc) - &(p)->code[0]) - 1)

inline int pcRel(const Instruction* pc, LuaProto* p) {
  int offset = (int)((pc-1) - p->code.begin());
  return offset;
}



l_noret luaG_typeerror (const LuaValue *o, const char *opname);
l_noret luaG_concaterror (StkId p1, StkId p2);
l_noret luaG_ordererror (const LuaValue *p1, const LuaValue *p2);
l_noret luaG_runerror (const char *fmt, ...);
l_noret luaG_errormsg ();

#endif
