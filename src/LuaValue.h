#pragma once

#include "LuaObject.h"
#include "LuaTypes.h"

#include <assert.h>

class TValue {
public:

  TValue() {
    type_ = LUA_TNIL;
    bytes_ = 0;
  }

  TValue(LuaType type, uint64_t data) {
    type_ = type;
    bytes_ = data;
  }

  //static TValue nil;
  //static TValue none;

  static TValue Nil()   { return TValue(LUA_TNIL, 0); }
  static TValue None()  { return TValue(LUA_TNONE, 0); }

  static TValue LightUserdata(const void* p);
  static TValue LightFunction(lua_CFunction f);

  explicit TValue(LuaObject* o)  { type_ = o->type(); bytes_ = 0; object_ = o; }
  explicit TValue(bool v)        { type_ = LUA_TBOOLEAN; bytes_ = v ? 1 : 0; }
  explicit TValue(int32_t v)     { type_ = LUA_TNUMBER; number_ = v; }
  explicit TValue(int64_t v)     { type_ = LUA_TNUMBER; number_ = (double)v; }
  explicit TValue(uint32_t v)    { type_ = LUA_TNUMBER; number_ = v; }
  explicit TValue(uint64_t v)    { type_ = LUA_TNUMBER; number_ = (double)v; }
  explicit TValue(double v)      { type_ = LUA_TNUMBER; number_ = v; }

  // Assignment operators

  void operator = (LuaObject* o) { type_ = o->type(); bytes_ = 0; object_ = o; }
  void operator = (TValue v)     { type_ = v.type_; bytes_ = v.bytes_; }
  void operator = (double v)     { type_ = LUA_TNUMBER; number_ = v; }
  void operator = (int v)        { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (size_t v)     { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (int64_t v)    { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (bool v)       { type_ = LUA_TBOOLEAN; bytes_ = v ? 1 : 0; }

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

  bool isCollectable() const   { return type_ >= LUA_TSTRING; }
  bool isFunction() const      { return (type_ == LUA_TCCL) || (type_ == LUA_TLCL) || (type_ == LUA_TLCF); }

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
  bool isProto() const         { return type_ == LUA_TPROTO; }

  bool isCClosure() const      { return type_ == LUA_TCCL; }
  bool isLClosure() const      { return type_ == LUA_TLCL; }
  bool isLightFunction() const { return type_ == LUA_TLCF; }

  // A 'true' value is either a true boolean or a non-Nil.
  // TODO(aappleby): this means that a None is true... might want to fix that.
  bool isTrue() const {
    if(isBool()) {
      return bytes_ ? true : false;
    } else {
      return !isNil();
    }
  }

  // A 'false' value is either a false boolean or a Nil.
  bool isFalse() const {
    if(isBool()) {
      return bytes_ ? false : true;
    } else {
      return isNil();
    }
  }

  //----------
  // These conversion operations return None if they fail.

  TValue convertToNumber() const;
  TValue convertToString() const;

  //----------

  bool isWhite() const;

  bool isLiveColor() const;

  //----------

  bool       getBool() const          { assert(isBool()); return bytes_ ? true : false; }
  int        getInteger() const       { return (int)number_; }
  double     getNumber() const        { assert(isNumber()); return number_; }
  LuaObject* getObject() const        { assert(isCollectable()); return object_; }
  Closure*   getCClosure()            { assert(isCClosure()); return reinterpret_cast<Closure*>(object_); }
  Closure*   getLClosure()            { assert(isLClosure()); return reinterpret_cast<Closure*>(object_); }
  TString*   getString() const        { assert(isString()); return reinterpret_cast<TString*>(object_); }
  Table*     getTable() const         { assert(isTable()); return reinterpret_cast<Table*>(object_); }
  Udata*     getUserdata() const      { assert(isUserdata()); return reinterpret_cast<Udata*>(object_); }
  void*      getLightUserdata() const { assert(isLightUserdata()); return pointer_; }
  lua_State* getThread() const        { assert(isThread()); return reinterpret_cast<lua_State*>(object_); }
  lua_CFunction getLightFunction() const { assert(isLightFunction()); return callback_; }

  uint64_t   getRawBytes() const { return bytes_; }

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
