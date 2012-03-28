#pragma once
#include "LuaTypes.h"
#include <assert.h>

class TValue {
public:

  TValue() { tt_ = LUA_TNIL; bytes = 0; }

  static TValue nil;

  explicit TValue(int v)    { tt_ = LUA_TNUMBER; n = v; }
  explicit TValue(double v) { tt_ = LUA_TNUMBER; n = v; }

  explicit TValue(TString* v) {
    bytes = 0;
    tt_ = ctb(LUA_TSTRING);
    gc = (LuaObject*)v;
    sanityCheck();
  }

  // Conversion operators

  operator Table*() { return getTable(); }

  // Assignment operators

  void operator = ( TValue const & V );
  void operator = ( TValue * pV );

  void operator = (double v)  { tt_ = LUA_TNUMBER; n = v; }
  void operator = (int v)     { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = (size_t v)  { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = (int64_t v) { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = (bool v)    { tt_ = LUA_TBOOLEAN; bytes = v ? 1 : 0; }

  void operator = (TString* v ) {
    bytes = 0;
    gc = (LuaObject*)v;
    tt_ = ctb(LUA_TSTRING);
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

  bool isNil() const           { return rawtype() == LUA_TNIL; }
  bool isNotNil() const        { return rawtype() != LUA_TNIL; }

  bool isBool() const          { return rawtype() == LUA_TBOOLEAN; }
  bool isInteger() const { return isNumber() && (n == (int)n); }
  bool isNumber() const        { return rawtype() == LUA_TNUMBER; }

  bool isCollectable() const   { return (rawtype() & BIT_ISCOLLECTABLE) != 0; }

  bool isLightUserdata() const { return rawtype() == LUA_TLIGHTUSERDATA; }

  bool isString() const        { return tagtype() == LUA_TSTRING; }
  bool isTable() const         { return tagtype() == LUA_TTABLE; }
  bool isUserdata() const      { return tagtype() == LUA_TUSERDATA; }
  bool isThread() const        { return rawtype() == (LUA_TTHREAD | BIT_ISCOLLECTABLE); }
  bool isProto() const         { return tagtype() == LUA_TPROTO; }
  bool isUpval() const         { return tagtype() == LUA_TUPVAL; }
  bool isDeadKey() const       { return rawtype() == LUA_TDEADKEY; }

  bool isFunction() const      { return basetype() == LUA_TFUNCTION; }
  bool isClosure() const       { return (rawtype() & 0x1F) == LUA_TFUNCTION; }
  bool isCClosure() const      { return rawtype() == (LUA_TCCL | BIT_ISCOLLECTABLE); }
  bool isLClosure() const      { return rawtype() == (LUA_TLCL | BIT_ISCOLLECTABLE); }
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




/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/* raw type tag of a TValue */
#define rttype(o)	((o)->tt_)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
#define ttype(o)	(rttype(o) & 0x3F)


/* type tag of a TValue with no variants (bits 0-3) */
#define ttypenv(o)	(rttype(o) & 0x0F)


/* Macros to test type */
#define checktag(o,t)		      (rttype(o) == (t))

#define ttisequal(o1,o2)	    (rttype(o1) == rttype(o2))

/* Macros to access values */
#define pvalue(o)	            check_exp((o)->isLightUserdata(), (o)->p)
#define uvalue(o)	            check_exp((o)->isUserdata(), reinterpret_cast<Udata*>((o)->gc))
#define clvalue(o)	          check_exp((o)->isClosure(), reinterpret_cast<Closure*>((o)->gc))
#define clLvalue(o)	          check_exp((o)->isLClosure(), reinterpret_cast<Closure*>((o)->gc))
#define clCvalue(o)	          check_exp((o)->isCClosure(), reinterpret_cast<Closure*>((o)->gc))
#define fvalue(o)	            check_exp((o)->isLightFunction(), (o)->f)
#define hvalue(o)	            check_exp((o)->isTable(), reinterpret_cast<Table*>((o)->gc))
#define thvalue(o)	          check_exp((o)->isThread(), reinterpret_cast<lua_State*>((o)->gc))

/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	        check_exp((o)->isDeadKey(), cast(void *, (o)->gc))

#define l_isfalse(o)	((o)->isNil() || ((o)->isBool() && !(o)->getBool()))

/* Macros to set values */
#define settt_(o,t)	((o)->tt_=(t))

#define setfvalue(obj,x) \
  { TValue *io=(obj); io->bytes = 0; io->f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); io->bytes = 0; io->p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setgcovalue(obj,x) \
  { TValue *io=(obj); io->bytes = 0; LuaObject *i_g=(x); \
    io->gc=i_g; settt_(io, ctb(i_g->tt)); }

#define setuvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TUSERDATA)); \
    io->sanityCheck(); }

#define setthvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TTHREAD)); \
    io->sanityCheck(); }

#define setclLvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TLCL)); \
    io->sanityCheck(); }

#define setclCvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TCCL)); \
    io->sanityCheck(); }

#define sethvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TTABLE)); \
    io->sanityCheck(); }

#define setptvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TPROTO)); \
    io->sanityCheck(); }

void setobj(TValue* obj1, const TValue* obj2);


#define luai_checknum(L,o,c)	{ /* empty */ }

