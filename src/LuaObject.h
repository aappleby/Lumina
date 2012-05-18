#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class LuaObject : public LuaBase {
public:

  LuaObject(LuaType type);
  virtual ~LuaObject();

  virtual void linkGC(LuaList& gclist);
  virtual void linkGC(LuaList& list, LuaObject* prev, LuaObject* next);
  virtual void unlinkGC(LuaList& gclist);

  enum Color {
    WHITE0 = 1,
    WHITE1 = 2,
    GRAY = 3,
    BLACK = 4,
  };

  static const Color colorA;
  static const Color colorB;

  Color getColor() const { return color_; }
  void setColor(Color c) { color_  = c; }

  bool isBlack();
  bool isWhite();
  bool isGray();
  bool isLiveColor();
  bool isDeadColor();
  bool isDead();

  void makeLive();

  virtual void VisitGC(LuaGCVisitor& visitor) = 0;
  virtual int PropagateGC(LuaGCVisitor& visitor) = 0;

  //----------

  LuaType type() const { return type_; }
  const char* typeName() const; 

  bool isString()   { return type_ == LUA_TSTRING; }
  bool isTable()    { return type_ == LUA_TTABLE; }
  bool isLClosure() { return type_ == LUA_TLCL; }
  bool isCClosure() { return type_ == LUA_TCCL; }
  bool isUserdata() { return type_ == LUA_TBLOB; }
  bool isThread()   { return type_ == LUA_TTHREAD; }
  bool isProto()    { return type_ == LUA_TPROTO; }
  bool isUpval()    { return type_ == LUA_TUPVALUE; }

  //----------
  // Flag read/write

  bool isFinalized();
  void setFinalized();
  void clearFinalized();

  bool isSeparated();
  void setSeparated();
  void clearSeparated();

  bool isFixed();
  void setFixed();
  void clearFixed();

  bool isOld();
  virtual void setOld();
  void clearOld();

  bool isTestGray();
  void setTestGray();
  void clearTestGray();

  //----------

  LuaObject* getPrev() const { return prev_; }
  LuaObject* getNext() const { return next_; }

  LuaObject* getNextGray() const { return next_gray_; }

  //----------

private:

  friend class LuaList;
  friend class LuaGraylist;

  void setPrev(LuaObject* o) { prev_ = o; }
  void setNext(LuaObject* o) { next_ = o; }

  void setNextGray(LuaObject* o) { next_gray_ = o; }

  LuaObject* prev_;
  LuaObject* next_;

  LuaObject* prev_gray_;
  LuaObject* next_gray_;

  LuaType type_;
  uint8_t flags_;
  Color color_;
};
