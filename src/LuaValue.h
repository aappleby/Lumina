#pragma once
#include "LuaTypes.h"
#include <assert.h>

#define l_isfalse(o)	((o)->isNil() || ((o)->isBool() && !(o)->getBool()))

void setobj(TValue* obj1, const TValue* obj2);

class TValue {
public:

  TValue() { tt_ = LUA_TNIL; bytes = 0; }
  TValue(int type, uint64_t data) {
    tt_ = type;
    bytes = data;
  }

  static TValue nil;
  static TValue none;

  static TValue LightUserdata(void* p);
  static TValue LightFunction(lua_CFunction f);
  static TValue LClosure(Closure* c);
  static TValue CClosure(Closure* c);

  explicit TValue(int v)    { tt_ = LUA_TNUMBER; n = v; }
  explicit TValue(double v) { tt_ = LUA_TNUMBER; n = v; }

  explicit TValue(TString* v);
  explicit TValue(LuaObject* o);

  // Conversion operators

  operator Table*() { return getTable(); }

  // Assignment operators

  void operator = (TValue const & V);
  void operator = (TValue * pV);

  void operator = (LuaObject* o);

  void operator = (double v)  { tt_ = LUA_TNUMBER; n = v; }
  void operator = (int v)     { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = (size_t v)  { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = (int64_t v) { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = (bool v)    { tt_ = LUA_TBOOLEAN; bytes = v ? 1 : 0; }

  void operator = (TString* v ) {
    bytes = 0;
    gc = (LuaObject*)v;
    tt_ = LUA_TSTRING;
    sanityCheck();
  }

  void operator = (Proto* p) {
    bytes = 0;
    gc = (LuaObject*)p;
    tt_ = LUA_TPROTO;
    sanityCheck();
  }

  // Comparison operators

  bool operator == (TValue const& v) {
    return (tt_ == v.tt_) && (bytes == v.bytes);
  }

  bool operator != (TValue const& v) {
    return !(*this == v);
  }

  bool operator == (int v) {
    if(!isNumber()) return false;
    return n == v;
  }

  // stuff

  bool isCollectable() const;
  bool isFunction() const;

  bool isNil() const           { return rawtype() == LUA_TNIL; }
  bool isNotNil() const        { return rawtype() != LUA_TNIL; }

  bool isBool() const          { return rawtype() == LUA_TBOOLEAN; }
  bool isNumber() const        { return rawtype() == LUA_TNUMBER; }

  bool isInteger() const       { return isNumber() && (n == (int)n); }

  bool isLightUserdata() const { return rawtype() == LUA_TLIGHTUSERDATA; }

  bool isString() const        { return tagtype() == LUA_TSTRING; }
  bool isTable() const         { return tagtype() == LUA_TTABLE; }
  bool isUserdata() const      { return tagtype() == LUA_TUSERDATA; }
  bool isThread() const        { return rawtype() == LUA_TTHREAD; }
  bool isUpval() const         { return tagtype() == LUA_TUPVAL; }
  bool isDeadKey() const       { return rawtype() == LUA_TDEADKEY; }
  bool isProto() const         { return rawtype() == LUA_TPROTO; }

  bool isClosure() const       { return (rawtype() & 0x1F) == LUA_TFUNCTION; }
  bool isCClosure() const      { return rawtype() == LUA_TCCL; }
  bool isLClosure() const      { return rawtype() == LUA_TLCL; }
  bool isLightFunction() const { return rawtype() == LUA_TLCF; }

  void setDeadKey() { tt_ = LUA_TDEADKEY; }

  //----------

  bool isWhite() const;

  //----------

  bool getBool() const { assert(isBool()); return b ? true : false; }
  int getInteger() const { return (int)getNumber(); }

  double getNumber() const { assert(isNumber()); return n; }

  LuaObject* getObject() const { assert(isCollectable()); return gc; }
  LuaObject* getDeadKey() { assert(isDeadKey());     return gc; }

  Closure* getClosure()  { assert(isClosure()); return reinterpret_cast<Closure*>(gc); }
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

  int32_t rawtype() const  { return tt_; }
  int32_t tagtype() const  { return tt_ & 0x3f; }
  int32_t basetype() const { return tt_ & 0x0f; }

  void clear() { bytes = 0; tt_ = 0; }

  void setBool  (int x)     { bytes = x ? 1 : 0; tt_ = LUA_TBOOLEAN; }
  void setValue (TValue* x) { bytes = x->bytes; tt_ = x->tt_; }

  void sanityCheck() const;
  void typeCheck() const;

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
  int32_t tt_;
};
