#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class LuaObject : public LuaBase {
public:

  LuaObject(LuaType type);
  virtual ~LuaObject();

  void linkGC(LuaObject** gcHead);

  void sanityCheck();

  uint8_t getFlags();

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

  virtual void VisitGC(LuaGCVisitor& visitor) = 0;
  virtual int PropagateGC(LuaGCVisitor& visitor) = 0;

  static const Color colorA;
  static const Color colorB;

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
  virtual void setOld();
  void clearOld();

  bool isTestGray();
  void setTestGray();
  void clearTestGray();

  //----------

  LuaObject *next_;
  LuaObject *next_gray_;

private:
  LuaType type_;
  uint8_t flags_;
  Color color_;
};
