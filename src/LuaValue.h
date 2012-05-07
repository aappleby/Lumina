#pragma once

#include "LuaObject.h"
#include "LuaTypes.h"

#include <assert.h>

class LuaValue {
public:

  LuaValue() {
    type_ = LUA_TNIL;
    bytes_ = 0;
  }

  LuaValue(LuaType type, uint64_t data) {
    type_ = type;
    bytes_ = data;
  }

  LuaValue(const LuaValue& v) {
    type_ = v.type_;
    bytes_ = v.bytes_;
  }

  static LuaValue Nil()   { return LuaValue(LUA_TNIL, 0); }
  static LuaValue None()  { return LuaValue(LUA_TNONE, 0); }
  static LuaValue Pointer(const void* p) { return LuaValue(LUA_TPOINTER, (uint64_t)p); }

  LuaValue(LuaObject* o)  { type_ = o->type(); bytes_ = 0; object_ = o; }
  LuaValue(bool v)        { type_ = LUA_TBOOLEAN; bytes_ = v ? 1 : 0; }
  LuaValue(LuaCallback f) { type_ = LUA_TCALLBACK; bytes_ = 0; callback_ = f; }

  LuaValue(float v)       { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(double v)      { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(int8_t v)      { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(uint8_t v)     { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(int16_t v)     { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(uint16_t v)    { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(int32_t v)     { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(uint32_t v)    { type_ = LUA_TNUMBER; number_ = v; }
  LuaValue(int64_t v)     { type_ = LUA_TNUMBER; number_ = (double)v; }
  LuaValue(uint64_t v)    { type_ = LUA_TNUMBER; number_ = (double)v; }

  // Assignment operators

  void operator = (LuaObject* o) { type_ = o->type(); bytes_ = 0; object_ = o; }
  void operator = (LuaValue v)   { type_ = v.type_; bytes_ = v.bytes_; }
  void operator = (double v)     { type_ = LUA_TNUMBER; number_ = v; }
  void operator = (int v)        { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (size_t v)     { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (int64_t v)    { type_ = LUA_TNUMBER; number_ = (double)v; }
  void operator = (bool v)       { type_ = LUA_TBOOLEAN; bytes_ = v ? 1 : 0; }

  // Comparison operators.

  // This will return false for positive and negative zero, that's a known issue.
  bool operator == (LuaValue const& v) const {
    return (type_ == v.type_) && (bytes_ == v.bytes_);
  }

  bool operator != (LuaValue const& v) const {
    return !(*this == v);
  }

  // stuff

  bool isCollectable() const  { return type_ >= LUA_TSTRING; }
  bool isFunction() const     { return (type_ == LUA_TCCL) || (type_ == LUA_TLCL) || (type_ == LUA_TCALLBACK); }

  bool isNil() const          { return type_ == LUA_TNIL; }
  bool isNotNil() const       { return type_ != LUA_TNIL; }
  bool isNone() const         { return type_ == LUA_TNONE; }

  bool isBool() const         { return type_ == LUA_TBOOLEAN; }
  bool isNumber() const       { return type_ == LUA_TNUMBER; }
  bool isInteger() const      { return isNumber() && (number_ == (int)number_); }

  bool isPointer() const      { return type_ == LUA_TPOINTER; }
  bool isString() const       { return type_ == LUA_TSTRING; }
  bool isTable() const        { return type_ == LUA_TTABLE; }
  bool isBlob() const         { return type_ == LUA_TBLOB; }
  bool isThread() const       { return type_ == LUA_TTHREAD; }
  bool isUpval() const        { return type_ == LUA_TUPVALUE; }
  bool isProto() const        { return type_ == LUA_TPROTO; }

  bool isClosure() const      { return (type_ == LUA_TCCL) || (type_ == LUA_TLCL); }
  bool isCClosure() const     { return type_ == LUA_TCCL; }
  bool isLClosure() const     { return type_ == LUA_TLCL; }
  bool isCallback() const     { return type_ == LUA_TCALLBACK; }

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

  LuaValue convertToNumber() const;
  LuaValue convertToString() const;

  //----------

  bool isWhite() const;
  bool isLiveColor() const;

  //----------

  bool         getBool() const      { assert(isBool()); return bytes_ ? true : false; }
  int          getInteger() const   { assert(isNumber()); return (int)number_; }
  double       getNumber() const    { assert(isNumber()); return number_; }
  LuaObject*   getObject() const    { assert(isCollectable()); return object_; }
  LuaClosure*  getClosure()         { assert(isClosure()); return reinterpret_cast<LuaClosure*>(object_); }
  LuaClosure*  getCClosure()        { assert(isCClosure()); return reinterpret_cast<LuaClosure*>(object_); }
  LuaClosure*  getLClosure()        { assert(isLClosure()); return reinterpret_cast<LuaClosure*>(object_); }
  LuaString*   getString() const    { assert(isString()); return reinterpret_cast<LuaString*>(object_); }
  LuaTable*    getTable() const     { assert(isTable()); return reinterpret_cast<LuaTable*>(object_); }
  LuaBlob*     getBlob() const      { assert(isBlob()); return reinterpret_cast<LuaBlob*>(object_); }
  void*        getPointer() const   { assert(isPointer()); return pointer_; }
  LuaThread*   getThread() const    { assert(isThread()); return reinterpret_cast<LuaThread*>(object_); }
  LuaCallback  getCallback() const  { assert(isCallback()); return callback_; }
  uint64_t     getRawBytes() const  { return bytes_; }

  //----------

  LuaType type() const  { return type_; }
  const char* typeName() const;

  void sanityCheck() const;
  void typeCheck() const;

  uint32_t hashValue() const;

private:

  union {
    LuaObject* object_;
    void* pointer_;
    LuaCallback callback_;
    double number_;
    uint64_t bytes_;
    struct {
      uint32_t lowbytes_;
      uint32_t highbytes_;
    } halves_;
  };

  LuaType type_;
};
