#include "LuaObject.h"

#include "lgc.h"
#include "lstate.h"

#include "LuaGlobals.h"

void *luaM_alloc_ (size_t size, int type, int pool);

/*
LuaObject::LuaObject(int type, LuaObject** list) {
  if (list == NULL)
    list = &thread_G->allgc;  // standard list for collectable objects
  
  marked = luaC_white(thread_G);
  tt = type;
  next = *list;
  *list = this;
}
*/

/*
void * LuaObject::operator new(size_t size, int type) {
  void* blob = luaM_alloc_(size, type, LAP_OBJECT);
  return blob;
}

void LuaObject::operator delete(void* blob, int type) {
}
*/

void LuaObject::Init(int type, LuaObject** list) {
  if(list == NULL) list = &thread_G->allgc;

  marked = luaC_white(thread_G);
  tt = type;

  next = *list;
  *list = this;
}

bool LuaObject::isDead() {
  uint8_t live = (marked ^ WHITEBITS) & (thread_G->currentwhite ^ WHITEBITS);
  return !live;
}