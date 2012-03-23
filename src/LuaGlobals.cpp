#include "LuaGlobals.h"

LuaObject*& getGlobalGCHead() {
  return thread_G->allgc;
}