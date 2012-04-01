#pragma once
#include "LuaBase.h"
#include "LuaTypes.h"

class LuaObject : public LuaBase {
public:

  LuaObject(int type);
  ~LuaObject();

  void linkGC(LuaObject*& gcHead);

  void sanityCheck();

  uint8_t getFlags();

  bool isDeadKey() { return tt == LUA_TDEADKEY; }

  bool isDead();
  bool isWhite();
  bool isGray();

  void setWhite();

  void whiteToGray();
  void blackToGray();
  void grayToBlack();
  void stringmark();

  void changeWhite();

  //----------

  bool isString()   { return tt == LUA_TSTRING; }
  bool isTable()    { return tt == LUA_TTABLE; }
  bool isLClosure() { return tt == LUA_TFUNCTION; }
  bool isCClosure() { return tt == LUA_TFUNCTION; }
  bool isUserdata() { return tt == LUA_TUSERDATA; }
  bool isThread()   { return tt == LUA_TTHREAD; }
  bool isProto()    { return tt == LUA_TPROTO; }
  bool isUpval()    { return tt == LUA_TUPVAL; }

  //----------
  // Flag read/write

  bool isBlack();
  void setBlack();
  void clearBlack();

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
  uint8_t tt;

  static int instanceCounts[256];

private:
  uint8_t marked;
};
