/*
** $Id: lmathlib.c,v 1.80 2011/07/05 12:49:35 roberto Exp $
** Standard mathematical library
** See Copyright Notice in lua.h
*/

#include "LuaState.h"

#include <stdlib.h>
#include <math.h>

#define lmathlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK



#undef PI
#define PI (3.14159265358979323846)
#define RADIANS_PER_DEGREE (PI/180.0)


/* macro 'l_tg' allows the addition of an 'l' or 'f' to all math operations */
#if !defined(l_tg)
#define l_tg(x)		(x)
#endif

template<typename T>
T checkarg(LuaThread* L, int idx);

template<>
double checkarg<double>(LuaThread* L, int idx) {
  return luaL_checknumber(L, idx);
}

template<>
int checkarg<int>(LuaThread* L, int idx) {
  return luaL_checkint(L, idx);
}

template<typename T>
void pushresult(LuaThread* L, T result);

template<>
void pushresult(LuaThread* L, double result) {
  lua_pushnumber(L, result);
}

template<>
void pushresult(LuaThread* L, int result) {
  lua_pushinteger(L, result);
}

template<typename Param1, typename Result1, Result1 (*F)(Param1)>
int Bind_1_1(LuaThread* L) {
  THREAD_CHECK(L);
  Param1 x = checkarg<Param1>(L, 1);
  Result1 result = F(x);
  pushresult<Result1>(L, result);
  return 1;
}

template<typename Param1, typename Param2, typename Result1, Result1 (*F)(Param1, Param2)>
int Bind_2_1(LuaThread* L) {
  THREAD_CHECK(L);
  Param1 x = checkarg<Param1>(L, 1);
  Param2 y = checkarg<Param2>(L, 2);
  Result1 result = F(x, y);
  pushresult<Result1>(L, result);
  return 1;
}

static int math_modf (LuaThread *L) {
  THREAD_CHECK(L);
  double ip;
  double fp = l_tg(modf)(luaL_checknumber(L, 1), &ip);
  lua_pushnumber(L, ip);
  lua_pushnumber(L, fp);
  return 2;
}

template<typename Param1, typename Result1, typename Result2, Result1 (*F)(Param1, Result2*) >
int Bind_1_2b(LuaThread* L) {
  THREAD_CHECK(L);
  Param1 x = checkarg<Param1>(L, 1);
  Result2 result2;
  Result1 result1 = F(x, &result2);
  pushresult<Result2>(L, result2);
  pushresult<Result1>(L, result1);
  return 1;
}

template<double (*F)(double)>
int Math_1(LuaThread* L) {
  return Bind_1_1<double, double, F>(L);
}

template<double (*F)(double, double)>
int Math_2(LuaThread* L) {
  return Bind_2_1<double, double, double, F>(L);
}

static int math_log (LuaThread *L) {
  THREAD_CHECK(L);
  double x = luaL_checknumber(L, 1);
  double res;
  if (lua_isnoneornil(L, 2))
    res = l_tg(log)(x);
  else {
    double base = luaL_checknumber(L, 2);
    if (base == 10.0) res = l_tg(log10)(x);
    else res = l_tg(log)(x)/l_tg(log)(base);
  }
  lua_pushnumber(L, res);
  return 1;
}

static double deg ( double x ) {
  return x / RADIANS_PER_DEGREE;
}

static double rad ( double x ) {
  return x * RADIANS_PER_DEGREE;
}

static int math_frexp (LuaThread *L) {
  THREAD_CHECK(L);
  int e;
  lua_pushnumber(L, l_tg(frexp)(luaL_checknumber(L, 1), &e));
  lua_pushinteger(L, e);
  return 2;
}

static int math_ldexp (LuaThread *L) {
  return Bind_2_1<double, int, double, ldexp>(L);
}

static int math_min (LuaThread *L) {
  THREAD_CHECK(L);
  int n = L->stack_.getTopIndex();  /* number of arguments */
  double dmin = luaL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    double d = luaL_checknumber(L, i);
    if (d < dmin)
      dmin = d;
  }
  lua_pushnumber(L, dmin);
  return 1;
}


static int math_max (LuaThread *L) {
  THREAD_CHECK(L);
  int n = L->stack_.getTopIndex();  /* number of arguments */
  double dmax = luaL_checknumber(L, 1);
  int i;
  for (i=2; i<=n; i++) {
    double d = luaL_checknumber(L, i);
    if (d > dmax)
      dmax = d;
  }
  lua_pushnumber(L, dmax);
  return 1;
}


static int math_random (LuaThread *L) {
  THREAD_CHECK(L);
  /* the `%' avoids the (rare) case of r==1, and is needed also because on
     some systems (SunOS!) `rand()' may return a value larger than RAND_MAX */
  double r = (double)(rand()%RAND_MAX) / (double)RAND_MAX;
  switch (L->stack_.getTopIndex()) {  /* check number of arguments */
    case 0: {  /* no arguments */
      lua_pushnumber(L, r);  /* Number between 0 and 1 */
      break;
    }
    case 1: {  /* only upper limit */
      double u = luaL_checknumber(L, 1);
      luaL_argcheck(L, 1.0 <= u, 1, "interval is empty");
      lua_pushnumber(L, l_tg(floor)(r*u) + 1.0);  /* int in [1, u] */
      break;
    }
    case 2: {  /* lower and upper limits */
      double l = luaL_checknumber(L, 1);
      double u = luaL_checknumber(L, 2);
      luaL_argcheck(L, l <= u, 2, "interval is empty");
      lua_pushnumber(L, l_tg(floor)(r*(u-l+1)) + l);  /* int in [l, u] */
      break;
    }
    default: return luaL_error(L, "wrong number of arguments");
  }
  return 1;
}


static int math_randomseed (LuaThread *L) {
  THREAD_CHECK(L);
  srand(luaL_checkunsigned(L, 1));
  (void)rand(); /* discard first value to avoid undesirable correlations */
  return 0;
}


static const luaL_Reg mathlib[] = {
  {"abs",   Math_1<fabs> },
  {"acos",  Math_1<acos> },
  {"asin",  Math_1<asin> },
  {"atan2", Math_2<atan2> },
  {"atan",  Math_1<atan> },
  {"ceil",  Math_1<ceil> },
  {"cosh",  Math_1<cosh> },
  {"cos",   Math_1<cos> },
  {"deg",   Math_1<deg> },
  {"exp",   Math_1<exp> },
  {"floor", Math_1<floor> },
  {"fmod",  Math_2<fmod> },
  {"frexp", math_frexp},
  {"ldexp", math_ldexp},
  {"log",   math_log},
  {"max",   math_max},
  {"min",   math_min},
  {"modf",  math_modf},
  {"pow",   Math_2<pow>},
  {"rad",   Math_1<rad> },
  {"sinh",  Math_1<sinh> },
  {"sin",   Math_1<sin> },
  {"sqrt",  Math_1<sqrt> },
  {"tanh",  Math_1<tanh> },
  {"tan",   Math_1<tan> },
  {"random",     math_random},
  {"randomseed", math_randomseed},
  {NULL, NULL}
};


/*
** Open math library
*/
int luaopen_math (LuaThread *L) {
  THREAD_CHECK(L);

  lua_createtable(L, 0, sizeof(mathlib)/sizeof((mathlib)[0]) - 1);
  luaL_setfuncs(L,mathlib,0);

  lua_pushnumber(L, PI);
  lua_setfield(L, -2, "pi");
  lua_pushnumber(L, HUGE_VAL);
  lua_setfield(L, -2, "huge");
  return 1;
}

