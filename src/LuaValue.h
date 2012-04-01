#pragma once
#include "LuaTypes.h"
#include <assert.h>

#define l_isfalse(o)	((o)->isNil() || ((o)->isBool() && !(o)->getBool()))

class TValue {
public:

  TValue() { type_ = LUA_TNIL; bytes_ = 0; }
  TValue(LuaType type, uint64_t data) {
    type_ = type;
    bytes_ = data;
  }

  static TValue nil;
  static TValue none;

  static TValue LightUserdata(void* p);
  static TValue LightFunction(lua_CFunction f);
  static TValue LClosure(Closure* c);
  static TValue CClosure(Closure* c);

  explicit TValue(int v)    { type_ = LUA_TNUMBER; number_ = v; }
  explicit TValue(double v) { type_ = LUA_TNUMBER; number_ = v; }

  explicit TValue(TString* v);
  explicit TValue(LuaObject* o);

  // Conversion operators

  operator Table*() { return getTable(); }

  // Assignment operators

  void operator = (TValue V);
  void operator = (TValue * pV);

  void operator = (LuaObject* o);

  void operator = (double v)  { type_ = LUA_TNUMBER; number_ = v; }
  void operator = (int v)     { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (size_t v)  { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (int64_t v) { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (bool v)    { type_ = LUA_TBOOLEAN; bytes_ = v ? 1 : 0; }

  void operator = (TString* v ) {
    bytes_ = 0;
    object_ = (LuaObject*)v;
    type_ = LUA_TSTRING;
    sanityCheck();
  }

  void operator = (Proto* p) {
    bytes_ = 0;
    object_ = (LuaObject*)p;
    type_ = LUA_TPROTO;
    sanityCheck();
  }

  // Comparison operators.

  // This will return false for positive and negative zero, that's a known issue.
  bool operator == (TValue const& v) const {
    return (type_ == v.type_) && (bytes_ == v.bytes_);
  }

  bool operator != (TValue const& v) const {
    return !(*this == v);
  }

  bool operator == (int v) const {
    if(!isNumber()) return false;
    return number_ == v;
  }

  // stuff

  bool isCollectable() const;
  bool isFunction() const;

  bool isNil() const           { return type_ == LUA_TNIL; }
  bool isNotNil() const        { return type_ != LUA_TNIL; }
  bool isNone() const          { return type_ == LUA_TNONE; }

  bool isBool() const          { return type_ == LUA_TBOOLEAN; }
  bool isNumber() const        { return type_ == LUA_TNUMBER; }

  bool isInteger() const       { return isNumber() && (number_ == (int)number_); }

  bool isLightUserdata() const { return type_ == LUA_TLIGHTUSERDATA; }

  bool isString() const        { return type_ == LUA_TSTRING; }
  bool isTable() const         { return type_ == LUA_TTABLE; }
  bool isUserdata() const      { return type_ == LUA_TUSERDATA; }
  bool isThread() const        { return type_ == LUA_TTHREAD; }
  bool isUpval() const         { return type_ == LUA_TUPVAL; }
  bool isDeadKey() const       { return type_ == LUA_TDEADKEY; }
  bool isProto() const         { return type_ == LUA_TPROTO; }

  bool isCClosure() const      { return type_ == LUA_TCCL; }
  bool isLClosure() const      { return type_ == LUA_TLCL; }
  bool isLightFunction() const { return type_ == LUA_TLCF; }

  void setDeadKey() { type_ = LUA_TDEADKEY; }

  //----------

  bool isWhite() const;

  //----------

  bool       getBool() const          { assert(isBool()); return bytes_ ? true : false; }
  int        getInteger() const       { return (int)number_; }
  double     getNumber() const        { assert(isNumber()); return number_; }
  LuaObject* getObject() const        { assert(isCollectable()); return object_; }
  LuaObject* getDeadKey()             { assert(isDeadKey()); return object_; }
  Closure*   getCClosure()            { assert(isCClosure()); return reinterpret_cast<Closure*>(object_); }
  Closure*   getLClosure()            { assert(isLClosure()); return reinterpret_cast<Closure*>(object_); }
  TString*   getString() const        { assert(isString()); return reinterpret_cast<TString*>(object_); }
  Table*     getTable() const         { assert(isTable()); return reinterpret_cast<Table*>(object_); }
  Udata*     getUserdata() const      { assert(isUserdata()); return reinterpret_cast<Udata*>(object_); }
  void*      getLightUserdata() const { assert(isLightUserdata()); return pointer_; }
  lua_State* getThread() const        { assert(isThread()); return reinterpret_cast<lua_State*>(object_); }
  lua_CFunction getLightFunction() const { assert(isLightFunction()); return callback_; }

  //----------

  LuaType type() const  { return type_; }
  const char* typeName() const;

  void clear() { bytes_ = 0; type_ = LUA_TNIL; }

  void setBool  (int x)     { bytes_ = x ? 1 : 0; type_ = LUA_TBOOLEAN; }
  void setValue (TValue* x) { bytes_ = x->bytes_; type_ = x->type_; }

  void sanityCheck() const;
  void typeCheck() const;

  uint32_t hashValue() const;

private:

  union {
    LuaObject* object_;
    void* pointer_;
    lua_CFunction callback_;
    double number_;
    uint64_t bytes_;
    struct {
      uint32_t lowbytes_;
      uint32_t highbytes_;
    };
  };

  LuaType type_;
};
