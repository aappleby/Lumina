#include "LuaGlobals.h"
#include "LuaState.h"

LuaObject*& getGlobalGCHead() {
  return thread_G->allgc;
}

global_State::global_State() {
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
  //assert(o->next_gray_ == NULL);
  o->setColor(LuaObject::GRAY);
  o->next_gray_ = grayhead_;
  grayhead_ = o;
}

void global_State::PushGrayAgain(LuaObject* o) {
  //assert(o->next_gray_ == NULL);
  o->setColor(LuaObject::GRAY);
  o->next_gray_ = grayagain_;
  grayagain_ = o;
}

void global_State::PushWeak(LuaObject* o) {
  //assert(o->next_gray_ == NULL);
  o->setColor(LuaObject::GRAY);
  o->next_gray_ = weak_;
  weak_ = o;
}

void global_State::PushAllWeak(LuaObject* o) {
  //assert(o->next_gray_ == NULL);
  o->setColor(LuaObject::GRAY);
  o->next_gray_ = allweak_;
  allweak_ = o;
}

void global_State::PushEphemeron(LuaObject* o) {
  //assert(o->next_gray_ == NULL);
  o->setColor(LuaObject::GRAY);
  o->next_gray_ = ephemeron_;
  ephemeron_ = o;
}