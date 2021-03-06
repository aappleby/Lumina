/*
** $Id: lbitlib.c,v 1.16 2011/06/20 16:35:23 roberto Exp $
** Standard library for bitwise operations
** See Copyright Notice in lua.h
*/

#include "LuaState.h"

#define lbitlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK


/* number of bits to consider in a number */
#if !defined(LUA_NBITS)
#define LUA_NBITS	32
#endif


#define ALLONES		(~(((~(uint32_t)0) << (LUA_NBITS - 1)) << 1))

/* macro to trim extra bits */
#define trim(x)		((x) & ALLONES)


/* builds a number with 'n' ones (1 <= n <= LUA_NBITS) */
#define mask(n)		(~((ALLONES << 1) << ((n) - 1)))


typedef uint32_t b_uint;



static b_uint andaux (LuaThread *L) {
  THREAD_CHECK(L);
  int i, n = L->stack_.getTopIndex();
  b_uint r = ~(b_uint)0;
  for (i = 1; i <= n; i++)
    r &= luaL_checkunsigned(L, i);
  return trim(r);
}


static int b_and (LuaThread *L) {
  THREAD_CHECK(L);
  b_uint r = andaux(L);
  lua_pushunsigned(L, r);
  return 1;
}


static int b_test (LuaThread *L) {
  THREAD_CHECK(L);
  b_uint r = andaux(L);
  lua_pushboolean(L, r != 0);
  return 1;
}


static int b_or (LuaThread *L) {
  THREAD_CHECK(L);
  int i, n = L->stack_.getTopIndex();
  b_uint r = 0;
  for (i = 1; i <= n; i++)
    r |= luaL_checkunsigned(L, i);
  lua_pushunsigned(L, trim(r));
  return 1;
}


static int b_xor (LuaThread *L) {
  THREAD_CHECK(L);
  int i, n = L->stack_.getTopIndex();
  b_uint r = 0;
  for (i = 1; i <= n; i++)
    r ^= luaL_checkunsigned(L, i);
  lua_pushunsigned(L, trim(r));
  return 1;
}


static int b_not (LuaThread *L) {
  THREAD_CHECK(L);
  b_uint r = ~luaL_checkunsigned(L, 1);
  lua_pushunsigned(L, trim(r));
  return 1;
}


static int b_shift (LuaThread *L, b_uint r, int i) {
  THREAD_CHECK(L);
  if (i < 0) {  /* shift right? */
    i = -i;
    r = trim(r);
    if (i >= LUA_NBITS) r = 0;
    else r >>= i;
  }
  else {  /* shift left */
    if (i >= LUA_NBITS) r = 0;
    else r <<= i;
    r = trim(r);
  }
  lua_pushunsigned(L, r);
  return 1;
}


static int b_lshift (LuaThread *L) {
  THREAD_CHECK(L);
  return b_shift(L, luaL_checkunsigned(L, 1), luaL_checkint(L, 2));
}


static int b_rshift (LuaThread *L) {
  THREAD_CHECK(L);
  return b_shift(L, luaL_checkunsigned(L, 1), -luaL_checkint(L, 2));
}


static int b_arshift (LuaThread *L) {
  THREAD_CHECK(L);
  b_uint r = luaL_checkunsigned(L, 1);
  int i = luaL_checkint(L, 2);
  if (i < 0 || !(r & ((b_uint)1 << (LUA_NBITS - 1))))
    return b_shift(L, r, -i);
  else {  /* arithmetic shift for 'negative' number */
    if (i >= LUA_NBITS) r = ALLONES;
    else
      r = trim((r >> i) | ~(~(b_uint)0 >> i));  /* add signal bit */
    lua_pushunsigned(L, r);
    return 1;
  }
}


static int b_rot (LuaThread *L, int i) {
  THREAD_CHECK(L);
  b_uint r = luaL_checkunsigned(L, 1);
  i &= (LUA_NBITS - 1);  /* i = i % NBITS */
  r = trim(r);
  r = (r << i) | (r >> (LUA_NBITS - i));
  lua_pushunsigned(L, trim(r));
  return 1;
}


static int b_lrot (LuaThread *L) {
  THREAD_CHECK(L);
  return b_rot(L, luaL_checkint(L, 2));
}


static int b_rrot (LuaThread *L) {
  THREAD_CHECK(L);
  return b_rot(L, -luaL_checkint(L, 2));
}


/*
** get field and width arguments for field-manipulation functions,
** checking whether they are valid
*/
static int fieldargs (LuaThread *L, int farg, int *width) {
  THREAD_CHECK(L);
  int f = luaL_checkint(L, farg);
  int w = luaL_optint(L, farg + 1, 1);
  luaL_argcheck(L, 0 <= f, farg, "field cannot be negative");
  luaL_argcheck(L, 0 < w, farg + 1, "width must be positive");
  if (f + w > LUA_NBITS)
    luaL_error(L, "trying to access non-existent bits");
  *width = w;
  return f;
}


static int b_extract (LuaThread *L) {
  THREAD_CHECK(L);
  int w;
  b_uint r = luaL_checkunsigned(L, 1);
  int f = fieldargs(L, 2, &w);
  r = (r >> f) & mask(w);
  lua_pushunsigned(L, r);
  return 1;
}


static int b_replace (LuaThread *L) {
  THREAD_CHECK(L);
  int w;
  b_uint r = luaL_checkunsigned(L, 1);
  b_uint v = luaL_checkunsigned(L, 2);
  int f = fieldargs(L, 3, &w);
  int m = mask(w);
  v &= m;  /* erase bits outside given width */
  r = (r & ~(m << f)) | (v << f);
  lua_pushunsigned(L, r);
  return 1;
}


static const luaL_Reg bitlib[] = {
  {"arshift", b_arshift},
  {"band", b_and},
  {"bnot", b_not},
  {"bor", b_or},
  {"bxor", b_xor},
  {"btest", b_test},
  {"extract", b_extract},
  {"lrotate", b_lrot},
  {"lshift", b_lshift},
  {"replace", b_replace},
  {"rrotate", b_rrot},
  {"rshift", b_rshift},
  {NULL, NULL}
};



int luaopen_bit32 (LuaThread *L) {
  THREAD_CHECK(L);

  LuaTable* lib = new LuaTable();
  for(const luaL_Reg* cursor = bitlib; cursor->name; cursor++) {
    lib->set( cursor->name, cursor->func );
  }

  L->stack_.push(lib);
  return 1;
}

