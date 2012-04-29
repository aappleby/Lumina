#include "LuaCollector.h"

#include "LuaGlobals.h"
#include "LuaObject.h" // for LuaGCVisitor
#include "LuaString.h"
#include "LuaValue.h"

void LuaCollector::ClearGraylists() {
  grayhead_.Clear();
  grayagain_.Clear();
  weak_.Clear();
  allweak_.Clear();
  ephemeron_.Clear();
}

/*
** retraverse all gray lists. Because tables may be reinserted in other
** lists when traversed, traverse the original lists to avoid traversing
** twice the same table (which is not wrong, but inefficient)
*/
void LuaCollector::RetraverseGrays() {
  LuaGraylist old_weak;
  LuaGraylist old_grayagain;
  LuaGraylist old_ephemeron;

  old_weak.Swap(weak_);
  old_grayagain.Swap(grayagain_);
  old_ephemeron.Swap(ephemeron_);
  
  LuaGCVisitor v(this);

  grayhead_.PropagateGC(v);
  old_grayagain.PropagateGC(v);
  old_weak.PropagateGC(v);
  old_ephemeron.PropagateGC(v);
}

// TODO(aappleby): what the hell does this do?
void LuaCollector::ConvergeEphemerons() {
  int changed;
  do {
    LuaGraylist old_ephemeron;
    old_ephemeron.Swap(ephemeron_);

    changed = 0;
    while(!old_ephemeron.isEmpty()) {

      LuaObject* o = old_ephemeron.Pop();
      LuaGCVisitor v(this);
      o->PropagateGC(v);

      // If propagating through the ephemeron table turned any objects gray,
      // we have to re-propagate those objects as well. That could in turn
      // cause more things in ephemeron tables to turn gray, so we have to repeat.
      // the process until nothing gets turned gray.
      if (v.mark_count_) {
        grayhead_.PropagateGC(v);
        changed = 1;  /* will have to revisit all ephemeron tables */
      }
    }
  } while (changed);
}

//------------------------------------------------------------------------------

void LuaGCVisitor::VisitString(LuaString* s) {
  s->setColor(LuaObject::GRAY);
}

//------------------------------------------------------------------------------

void LuaGCVisitor::MarkValue(LuaValue v) {
  if(v.isCollectable()) {
    MarkObject(v.getObject());
  }
}

void LuaGCVisitor::MarkObject(LuaObject* o) {
  if(o == NULL) return;
  if(o->isGray()) {
    return;
  }
  if(o->isBlack()) {
    return;
  }

  mark_count_++;

  if(!o->isFixed()) {
    assert(o->isLiveColor());
  }

  o->VisitGC(*this);
  return;
}

//------------------------------------------------------------------------------

void LuaGCVisitor::PushGray(LuaObject* o) {
  parent_->grayhead_.Push(o);
}

void LuaGCVisitor::PushGrayAgain(LuaObject* o) {
  parent_->grayagain_.Push(o);
}

void LuaGCVisitor::PushWeak(LuaObject* o) {
  parent_->weak_.Push(o);
}

void LuaGCVisitor::PushAllWeak(LuaObject* o) {
  parent_->allweak_.Push(o);
}

void LuaGCVisitor::PushEphemeron(LuaObject* o) {
  parent_->ephemeron_.Push(o);
}

//------------------------------------------------------------------------------
