#include "LuaList.h"

#include "LuaTable.h"

void LuaGraylist::Clear() {
  while(head_) Pop();
}

void LuaGraylist::Sweep() {
  for (LuaObject* o = head_; o != NULL; o = o->next_gray_) {
    if(o->isTable()) {
      LuaTable *t = dynamic_cast<LuaTable*>(o);
      t->SweepWhite();
    }
  }
}

void LuaGraylist::SweepKeys() {
  for (LuaObject* o = head_; o != NULL; o = o->next_gray_) {
    if(o->isTable()) {
      LuaTable *t = dynamic_cast<LuaTable*>(o);
      t->SweepWhiteKeys();
    }
  }
}

void LuaGraylist::SweepValues() {
  for (LuaObject* o = head_; o != NULL; o = o->next_gray_) {
    if(o->isTable()) {
      LuaTable *t = dynamic_cast<LuaTable*>(o);
      t->SweepWhiteVals();
    }
  }
}

