#include "LuaGlobals.h"
#include "LuaState.h"

LuaObject** getGlobalGCHead() {
  return &thread_G->allgc;
}

global_State::global_State()
: uvhead(NULL)
{
  GCdebt_ = 0;
  isShuttingDown = false;
  totalbytes_ = sizeof(lua_State) + sizeof(global_State);
}

global_State::~global_State() {
}

void global_State::setGCDebt(size_t debt) {
  GCdebt_ = debt;
}

void global_State::incTotalBytes(int bytes) {
  totalbytes_ += bytes;
}

void global_State::incGCDebt(int debt) { 
  GCdebt_ += debt;
}

void global_State::PushGray(LuaObject* o) {
  grayhead_.Push(o);
}

void global_State::PushGrayAgain(LuaObject* o) {
  grayagain_.Push(o);
}

void global_State::PushWeak(LuaObject* o) {
  weak_.Push(o);
}

void global_State::PushAllWeak(LuaObject* o) {
  allweak_.Push(o);
}

void global_State::PushEphemeron(LuaObject* o) {
  ephemeron_.Push(o);
}