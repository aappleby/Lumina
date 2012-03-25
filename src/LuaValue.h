#pragma once
#include "LuaTypes.h"
#include <assert.h>

class TValue {
public:

  TValue() {
    tt_ = LUA_TNIL;
    bytes = 0;
  }

  explicit TValue(int v) {
    tt_ = LUA_TNUMBER;
    n = v;
  }

  explicit TValue(TString* v) {
    bytes = 0;
    tt_ = ctb(LUA_TSTRING);
    gc = (LuaObject*)v;
    sanitycheck();
  }

  // Conversion operators

  operator Table*() { return getTable(); }

  // Assignment operators

  void operator = ( TValue const & V );
  void operator = ( TValue * pV );

  void operator = ( int v ) { tt_ = LUA_TNUMBER; n = (double)v; }
  void operator = ( bool v ) { tt_ = LUA_TBOOLEAN; bytes = v ? 1 : 0; }

  void operator = (TString* v ) {
    bytes = 0;
    gc = (LuaObject*)v;
    tt_ = ctb(LUA_TSTRING);
    sanitycheck();
  }

  void operator = (double v) {
    n = v;
    tt_ = LUA_TNUMBER;
  }

  // Comparison operators

  bool operator == (TValue const& v) {
    return (tt_ == v.tt_) && (bytes == v.bytes);
  }

  bool operator != (TValue const& v) {
    return !(*this == v);
  }

  // stuff

  bool isCollectable() { return (rawtype() & BIT_ISCOLLECTABLE) != 0; }

  bool isNil() const           { return rawtype() == LUA_TNIL; }
  bool isBoolean() const       { return tagtype() == LUA_TBOOLEAN; }
  bool isLightUserdata() const { return tagtype() == LUA_TLIGHTUSERDATA; }
  bool isNumber() const        { return tagtype() == LUA_TNUMBER; }
  bool isString() const        { return tagtype() == LUA_TSTRING; }
  bool isTable() const         { return tagtype() == LUA_TTABLE; }
  bool isFunction() const      { return basetype() == LUA_TFUNCTION; }
  bool isUserdata() const      { return tagtype() == LUA_TUSERDATA; }
  bool isThread() const        { return tagtype() == LUA_TTHREAD; }
  bool isProto() const         { return tagtype() == LUA_TPROTO; }
  bool isUpval() const         { return tagtype() == LUA_TUPVAL; }
  bool isDeadKey() const       { return tagtype() == LUA_TDEADKEY; }

  //bool isCClosure() const      { return 
  bool isCClosure() const      { return rawtype() == (LUA_TCCL | BIT_ISCOLLECTABLE); }
  bool isLClosure() const      { return rawtype() == (LUA_TLCL | BIT_ISCOLLECTABLE); }
  bool isLightCFunc() const    { return tagtype() == LUA_TLCF; }

  bool isInteger() const { return isNumber() && (n == (int)n); }

  int getInteger() const { assert(isInteger()); return (int)n; }

  double getNumber() const { assert(isNumber()); return n; }

  LuaObject* getObject()  { assert(isCollectable()); return gc; }
  LuaObject* getDeadKey() { assert(isDeadKey());     return gc; }

  Closure* getCClosure() { assert(isCClosure()); return reinterpret_cast<Closure*>(gc); }
  Closure* getLClosure() { assert(isLClosure()); return reinterpret_cast<Closure*>(gc); }

  TString* getString() { assert(isString()); return reinterpret_cast<TString*>(gc); }
  Table*   getTable()  { assert(isTable()); return reinterpret_cast<Table*>(gc); }

  Table* getTable() const { assert(isTable()); return reinterpret_cast<Table*>(gc); }

  int32_t rawtype() const  { return tt_; }
  int32_t tagtype() const  { return tt_ & 0x3f; }
  int32_t basetype() const { return tt_ & 0x0f; }

  void clear() { bytes = 0; tt_ = 0; }

  void setBool  (int x)     { bytes = x ? 1 : 0; tt_ = LUA_TBOOLEAN; }
  void setValue (TValue* x) { bytes = x->bytes; tt_ = x->tt_; }

  void sanitycheck();

  union {
    LuaObject *gc;    /* collectable objects */
    void *p;         /* light userdata */
    int32_t b;           /* booleans */
    lua_CFunction f; /* light C functions */
    lua_Number n;    /* numbers */
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
#define ttisnumber(o)		      checktag((o), LUA_TNUMBER)
#define ttisnil(o)		        checktag((o), LUA_TNIL)
#define ttisboolean(o)		    checktag((o), LUA_TBOOLEAN)
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)
#define ttisstring(o)		      checktag((o), ctb(LUA_TSTRING))
#define ttistable(o)		      checktag((o), ctb(LUA_TTABLE))
#define ttisfunction(o)		    (ttypenv(o) == LUA_TFUNCTION)
#define ttisclosure(o)		    ((rttype(o) & 0x1F) == LUA_TFUNCTION)
#define ttisCclosure(o)		    checktag((o), ctb(LUA_TCCL))
#define ttisLclosure(o)		    checktag((o), ctb(LUA_TLCL))
#define ttislcf(o)		        checktag((o), LUA_TLCF)
#define ttisuserdata(o)		    checktag((o), ctb(LUA_TUSERDATA))
#define ttisthread(o)		      checktag((o), ctb(LUA_TTHREAD))
#define ttisdeadkey(o)		    checktag((o), LUA_TDEADKEY)

#define ttisequal(o1,o2)	    (rttype(o1) == rttype(o2))

/* Macros to access values */
#define nvalue(o)	            check_exp(ttisnumber(o), (o)->n)
#define gcvalue(o)	          check_exp(iscollectable(o), (o)->gc)
#define pvalue(o)	            check_exp(ttislightuserdata(o), (o)->p)
#define tsvalue(o)	          check_exp(ttisstring(o), reinterpret_cast<TString*>((o)->gc))
#define uvalue(o)	            check_exp(ttisuserdata(o), reinterpret_cast<Udata*>((o)->gc))
#define clvalue(o)	          check_exp(ttisclosure(o), reinterpret_cast<Closure*>((o)->gc))
#define clLvalue(o)	          check_exp(ttisLclosure(o), reinterpret_cast<Closure*>((o)->gc))
#define clCvalue(o)	          check_exp(ttisCclosure(o), reinterpret_cast<Closure*>((o)->gc))
#define fvalue(o)	            check_exp(ttislcf(o), (o)->f)
#define hvalue(o)	            check_exp(ttistable(o), reinterpret_cast<Table*>((o)->gc))
#define bvalue(o)	            check_exp(ttisboolean(o), (o)->b)
#define thvalue(o)	          check_exp(ttisthread(o), reinterpret_cast<lua_State*>((o)->gc))

/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	        check_exp(ttisdeadkey(o), cast(void *, (o)->gc))

#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))


#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests */
#define righttt(obj)		(ttypenv(obj) == gcvalue(obj)->tt)

//#define checkliveness(obj) assert(!iscollectable(obj) || (righttt(obj) && !isdead(gcvalue(obj))))
#define checkliveness(v) ((v)->sanitycheck())


/* Macros to set values */
#define settt_(o,t)	((o)->tt_=(t))

#define setnvalue(obj,x) \
  { TValue *io=(obj); io->n=(x); io->tt_=LUA_TNUMBER; }

#define changenvalue(o,x)	check_exp(ttisnumber(o), (o)->n=(x))

#define setnilvalue(obj) { TValue* io=(obj); io->bytes = 0; io->tt_ = LUA_TNIL; }

#define setfvalue(obj,x) \
  { TValue *io=(obj); io->bytes = 0; io->f=(x); settt_(io, LUA_TLCF); }

#define setpvalue(obj,x) \
  { TValue *io=(obj); io->bytes = 0; io->p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

#define setgcovalue(obj,x) \
  { TValue *io=(obj); io->bytes = 0; LuaObject *i_g=(x); \
    io->gc=i_g; settt_(io, ctb(gch(i_g)->tt)); }

#define setuvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(io); }

#define setthvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(io); }

#define setclLvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(io); }

#define setclCvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(io); }

#define sethvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(io); }

#define setptvalue(L,obj,x) \
  { THREAD_CHECK(L); TValue *io=(obj); io->bytes = 0; \
    io->gc=cast(LuaObject *, (x)); settt_(io, ctb(LUA_TPROTO)); \
    checkliveness(io); }

#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)

void setobj(TValue* obj1, const TValue* obj2);


#define luai_checknum(L,o,c)	{ /* empty */ }

