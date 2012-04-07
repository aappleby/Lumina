#include "LuaProto.h"

#include "LuaClosure.h"
#include "LuaString.h"

Proto::Proto() : LuaObject(LUA_TPROTO) {
  cache = NULL;
  numparams = 0;
  is_vararg = 0;
  maxstacksize = 0;
  linedefined = 0;
  lastlinedefined = 0;
  source = NULL;
}

void Proto::VisitGC(GCVisitor& visitor) {
  setColor(GRAY);
  visitor.PushGray(this);
}

int Proto::PropagateGC(GCVisitor& visitor) {
  setColor(BLACK);

  // allow cache to be collected
  // TODO(aappleby): wha?
  if (cache && cache->isWhite()) {
    cache = NULL;
  }

  visitor.MarkObject(source);

  // mark literals
  for (size_t i = 0; i < constants.size(); i++)
    visitor.MarkValue(constants[i]);

  // mark upvalue names
  for (size_t i = 0; i < upvalues.size(); i++) {
    visitor.MarkObject(upvalues[i].name);
  }

  // mark nested protos
  for (size_t i = 0; i < subprotos_.size(); i++) {
    visitor.MarkObject(subprotos_[i]);
  }

  // mark local-variable names
  for (size_t i = 0; i < locvars.size(); i++) {
    visitor.MarkObject(locvars[i].varname);
  }

  return TRAVCOST +
         (int)constants.size() +
         (int)upvalues.size() +
         (int)subprotos_.size() +
         (int)locvars.size();
}