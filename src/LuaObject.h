#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class GCVisitor {
public:
  void MarkValue(TValue v);
  void MarkObject(LuaObject* o);
  void PushGray(LuaObject* o);
};

class LuaObject : public LuaBase {
public:

  LuaObject(LuaType type);
  virtual ~LuaObject();

  void linkGC(LuaObject*& gcHead);

  void sanityCheck();

  uint8_t getFlags();

  bool isDeadKey() { return type_ == LUA_TDEADKEY; }

  bool isDead();

  bool isLiveColor();
  bool isDeadColor();

  enum Color {
    WHITE0 = 1,
    WHITE1 = 2,
    GRAY = 3,
    BLACK = 4,
  };

  Color getColor() const { return color_; }
  void setColor(Color c) { color_  = c; }

  bool isWhite();
  bool isGray();

  void makeLive();

  void whiteToGray();
  void blackToGray();
  void grayToBlack();
  void stringmark();

  virtual void VisitGC(GCVisitor& visitor);

  static const Color colorA;
  static const Color colorB;

  //----------

  LuaType type() const { return type_; }
  const char* typeName() const; 

  bool isString()   { return type_ == LUA_TSTRING; }
  bool isTable()    { return type_ == LUA_TTABLE; }
  bool isLClosure() { return type_ == LUA_TLCL; }
  bool isCClosure() { return type_ == LUA_TCCL; }
  bool isUserdata() { return type_ == LUA_TUSERDATA; }
  bool isThread()   { return type_ == LUA_TTHREAD; }
  bool isProto()    { return type_ == LUA_TPROTO; }
  bool isUpval()    { return type_ == LUA_TUPVAL; }

  //----------
  // Flag read/write

  bool isBlack();

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
  void setOld();
  void clearOld();

  bool isTestGray();
  void setTestGray();
  void clearTestGray();

  //----------

  LuaObject *next;
  LuaObject *next_gray_;

  static int instanceCounts[256];

private:
  LuaType type_;
  uint8_t flags_;
  Color color_;
};
