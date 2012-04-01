#pragma once
#include "LuaTypes.h"
#include <assert.h>

#define l_isfalse(o)	((o)->isNil() || ((o)->isBool() && !(o)->getBool()))

void setobj(TValue* obj1, const TValue* obj2);

class TValue {
public:

  TValue() { type_ = LUA_TNIL; bytes = 0; }
  TValue(int type, uint64_t data) {
    type_ = type;
    bytes = data;
  }

  static TValue nil;
  static TValue none;

  static TValue LightUserdata(void* p);
  static TValue LightFunction(lua_CFunction f);
  static TValue LClosure(Closure* c);
  static TValue CClosure(Closure* c);

  explicit TValue(int v)    { type_ = LUA_TNUMBER; n = v; }
  explicit TValue(double v) { type_ = LUA_TNUMBER; n = v; }

  explicit TValue(TString* v);
  explicit TValue(LuaObject* o);

  // Conversion operators

  operator Table*() { return getTable(); }

  // Assignment operators

  void operator = (TValue V);
  void operator = (TValue * pV);

  void operator = (LuaObject* o);

  void operator = (double v)  { type_ = LUA_TNUMBER; n = v; }
  void operator = (int v)     { type_ = LUA_TNUMBER; n = (double)v; }
  void operator = (size_t v)  { type_ = LUA_TNUMBER; n = (double)v; }
  void operator = (int64_t v) { type_ = LUA_TNUMBER; n = (double)v; }
  void operator = (bool v)    { type_ = LUA_TBOOLEAN; bytes = v ? 1 : 0; }

  void operator = (TString* v ) {
    bytes = 0;
    gc = (LuaObject*)v;
    type_ = LUA_TSTRING;
    sanityCheck();
  }

  void operator = (Proto* p) {
    bytes = 0;
    gc = (LuaObject*)p;
    type_ = LUA_TPROTO;
    sanityCheck();
  }

  // Comparison operators.

  // This will return false for positive and negative zero, that's a known issue.
  bool operator == (TValue const& v) const {
    return (type_ == v.type_) && (bytes == v.bytes);
  }

  bool operator != (TValue const& v) const {
    return !(*this == v);
  }

  bool operator == (int v) const {
    if(!isNumber()) return false;
    return n == v;
  }

  // stuff

  bool isCollectable() const;
  bool isFunction() const;

  bool isNil() const           { return type() == LUA_TNIL; }
  bool isNotNil() const        { return type() != LUA_TNIL; }

  bool isBool() const          { return type() == LUA_TBOOLEAN; }
  bool isNumber() const        { return type() == LUA_TNUMBER; }

  bool isInteger() const       { return isNumber() && (n == (int)n); }

  bool isLightUserdata() const { return type() == LUA_TLIGHTUSERDATA; }

  bool isString() const        { return type() == LUA_TSTRING; }
  bool isTable() const         { return type() == LUA_TTABLE; }
  bool isUserdata() const      { return type() == LUA_TUSERDATA; }
  bool isThread() const        { return type() == LUA_TTHREAD; }
  bool isUpval() const         { return type() == LUA_TUPVAL; }
  bool isDeadKey() const       { return type() == LUA_TDEADKEY; }
  bool isProto() const         { return type() == LUA_TPROTO; }

  bool isCClosure() const      { return type() == LUA_TCCL; }
  bool isLClosure() const      { return type() == LUA_TLCL; }
  bool isLightFunction() const { return type() == LUA_TLCF; }

  void setDeadKey() { type_ = LUA_TDEADKEY; }

  //----------

  bool isWhite() const;

  //----------

  bool getBool() const { assert(isBool()); return b ? true : false; }
  int getInteger() const { return (int)getNumber(); }

  double getNumber() const { assert(isNumber()); return n; }

  LuaObject* getObject() const { assert(isCollectable()); return gc; }
  LuaObject* getDeadKey() { assert(isDeadKey());     return gc; }

  Closure* getCClosure() { assert(isCClosure()); return reinterpret_cast<Closure*>(gc); }
  Closure* getLClosure() { assert(isLClosure()); return reinterpret_cast<Closure*>(gc); }

  TString* getString() const { assert(isString()); return reinterpret_cast<TString*>(gc); }
  Table*   getTable()  { assert(isTable()); return reinterpret_cast<Table*>(gc); }

  Table* getTable() const { assert(isTable()); return reinterpret_cast<Table*>(gc); }

  Udata* getUserdata() const { assert(isUserdata()); return reinterpret_cast<Udata*>(gc); }
  void* getLightUserdata() const { assert(isLightUserdata()); return p; }

  lua_State* getThread() const { assert(isThread()); return reinterpret_cast<lua_State*>(gc); }

  lua_CFunction getLightFunction() const { assert(isLightFunction()); return f; }

  //----------

  int32_t type() const  { return type_; }

  void clear() { bytes = 0; type_ = 0; }

  void setBool  (int x)     { bytes = x ? 1 : 0; type_ = LUA_TBOOLEAN; }
  void setValue (TValue* x) { bytes = x->bytes; type_ = x->type_; }

  void sanityCheck() const;
  void typeCheck() const;

private:

  union {
    LuaObject *gc;    /* collectable objects */
    void *p;         /* light userdata */
    int32_t b;           /* booleans */
    lua_CFunction f; /* light C functions */
    lua_Number n;
    uint64_t bytes;
    struct {
      uint32_t low;
      uint32_t high;
    };
  };

  int32_t type_;
};
