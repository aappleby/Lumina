#pragma once

#include "LuaList.h" // for LuaGrayList

class LuaCollector {
public:

  LuaCollector() {
  }

  ~LuaCollector() {
    ClearGraylists();
  }

  bool hasGrays() {
    return !grayhead_.isEmpty();
  }

  void ClearGraylists();
  void RetraverseGrays();
  void ConvergeEphemerons();

  LuaGraylist grayhead_;  // Topmost list of gray objects
  LuaGraylist grayagain_; // list of objects to be traversed atomically
  LuaGraylist weak_;      // list of tables with weak values
  LuaGraylist ephemeron_; // list of ephemeron tables (weak keys)
  LuaGraylist allweak_;   // list of all-weak tables
};

class LuaGCVisitor {
public:

  LuaGCVisitor(LuaCollector* parent)
  : parent_(parent),
    mark_count_(0)
  {
  }

  void VisitString   (LuaString* s);

  void MarkValue     (LuaValue v);
  void MarkObject    (LuaObject* o);

  void PushGray      (LuaObject* o);
  void PushGrayAgain (LuaObject* o);
  void PushWeak      (LuaObject* o);
  void PushAllWeak   (LuaObject* o);
  void PushEphemeron (LuaObject* o);

  LuaCollector* parent_;
  int mark_count_;
};
