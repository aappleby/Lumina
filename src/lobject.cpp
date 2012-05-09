/*
** $Id: lobject.c,v 2.55 2011/11/30 19:30:16 roberto Exp $
** Some generic functions over Lua objects
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaState.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>

#define lobject_c

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lvm.h"



/*
** converts an integer to a "floating point byte", represented as
** (eeeeexxx), where the real value is (1xxx) * 2^(eeeee - 1) if
** eeeee != 0 and (xxx) otherwise.
*/
int luaO_int2fb (unsigned int x) {
  int e = 0;  /* exponent */
  if (x < 8) return x;
  while (x >= 0x10) {
    x = (x+1) >> 1;
    e++;
  }
  return ((e+1) << 3) | (cast_int(x) - 8);
}


/* converts back */
int luaO_fb2int (int x) {
  int e = (x >> 3) & 0x1f;
  if (e == 0) return x;
  else return ((x & 7) + 8) << (e - 1);
}


int luaO_ceillog2 (unsigned int x) {
  static const uint8_t log_2[256] = {
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8
  };
  int l = 0;
  x--;
  while (x >= 256) { l += 8; x >>= 8; }
  return l + log_2[x];
}


double luaO_arith (int op, double v1, double v2) {
  switch (op) {
    case LUA_OPADD: return v1 + v2;
    case LUA_OPSUB: return v1 - v2;
    case LUA_OPMUL: return v1 * v2;
    case LUA_OPDIV: return v1 / v2;
    case LUA_OPMOD: return v1 - floor(v1/v2)*v2;
    case LUA_OPPOW: return pow(v1,v2);
    case LUA_OPUNM: return -v1;
    default: assert(0); return 0;
  }
}


int luaO_hexavalue (int c) {
  if (lisdigit(c)) return c - '0';
  else return ltolower(c) - 'a' + 10;
}


#if !defined(lua_strx2number)

#include <math.h>


static int isneg (const char **s) {
  if (**s == '-') { (*s)++; return 1; }
  else if (**s == '+') (*s)++;
  return 0;
}


static double readhexa (const char **s, double r, int *count) {
  for (; lisxdigit(cast_uchar(**s)); (*s)++) {  /* read integer part */
    r = (r * 16.0) + cast_num(luaO_hexavalue(cast_uchar(**s)));
    (*count)++;
  }
  return r;
}


/*
** convert an hexadecimal numeric string to a number, following
** C99 specification for 'strtod'
*/
static double lua_strx2number (const char *s, char **endptr) {
  double r = 0.0;
  int e = 0, i = 0;
  int neg = 0;  /* 1 if number is negative */
  *endptr = cast(char *, s);  /* nothing is valid yet */
  while (lisspace(cast_uchar(*s))) s++;  /* skip initial spaces */
  neg = isneg(&s);  /* check signal */
  if (!(*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X')))  /* check '0x' */
    return 0.0;  /* invalid format (no '0x') */
  s += 2;  /* skip '0x' */
  r = readhexa(&s, r, &i);  /* read integer part */
  if (*s == '.') {
    s++;  /* skip dot */
    r = readhexa(&s, r, &e);  /* read fractional part */
  }
  if (i == 0 && e == 0)
    return 0.0;  /* invalid format (no digit) */
  e *= -4;  /* each fractional digit divides value by 2^-4 */
  *endptr = cast(char *, s);  /* valid up to here */
  if (*s == 'p' || *s == 'P') {  /* exponent part? */
    int exp1 = 0;
    int neg1;
    s++;  /* skip 'p' */
    neg1 = isneg(&s);  /* signal */
    if (!lisdigit(cast_uchar(*s)))
      goto ret;  /* must have at least one digit */
    while (lisdigit(cast_uchar(*s)))  /* read exponent */
      exp1 = exp1 * 10 + *(s++) - '0';
    if (neg1) exp1 = -exp1;
    e += exp1;
  }
  *endptr = cast(char *, s);  /* valid up to here */
 ret:
  if (neg) r = -r;
  return ldexp(r, e);
}

#endif


int luaO_str2d (const char *s, size_t len, double *result) {
  char *endptr;
  if (strpbrk(s, "nN"))  /* reject 'inf' and 'nan' */
    return 0;
  else if (strpbrk(s, "xX"))  /* hexa? */
    *result = lua_strx2number(s, &endptr);
  else
    *result = strtod(s, &endptr);
  if (endptr == s) return 0;  /* nothing recognized */
  while (lisspace(cast_uchar(*endptr))) endptr++;
  return (endptr == s + len);  /* OK if no trailing characters */
}



static void pushstr (LuaThread *L, const char *str, int l) {
  THREAD_CHECK(L);
  LuaString* s = thread_G->strings_->Create(str, l);
  LuaResult result = L->stack_.push_reserve2(LuaValue(s));
  handleResult(result);
}


/* this function handles only `%d', `%c', %f, %p, and `%s' formats */
const char *luaO_pushvfstring (const char *fmt, va_list argp) {
  LuaResult result = LUA_OK;
  LuaThread* L = thread_L;
  int n = 0;
  for (;;) {
    const char *e = strchr(fmt, '%');
    if (e == NULL) break;

    LuaString* s = thread_G->strings_->Create(fmt, int(e - fmt));
    result = L->stack_.push_reserve2(LuaValue(s));
    handleResult(result);

    switch (*(e+1)) {
      case 's': {
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        pushstr(L, s, (int)strlen(s));
        break;
      }
      case 'c': {
        char buff;
        buff = cast(char, va_arg(argp, int));
        pushstr(L, &buff, 1);
        break;
      }
      case 'd': 
        {
          result = L->stack_.push_reserve2( LuaValue(va_arg(argp, int)) );
          handleResult(result);
          break;
        }
      case 'f': 
        {
          result = L->stack_.push_reserve2( LuaValue(va_arg(argp, double)) );
          handleResult(result);
          break;
        }
      case 'p': {
        char buff[4*sizeof(void *) + 8]; /* should be enough space for a `%p' */
        int l = sprintf(buff, "%p", va_arg(argp, void *));
        pushstr(L, buff, l);
        break;
      }
      case '%': {
        pushstr(L, "%", 1);
        break;
      }
      default: {
        result = luaG_runerror("invalid option " LUA_QL("%%%c") " to " LUA_QL("lua_pushfstring"), *(e + 1));
        handleResult(result);
      }
    }
    n += 2;
    fmt = e+2;
  }
  pushstr(L, fmt, (int)strlen(fmt));
  if (n > 0) luaV_concat(L, n + 1);
  return L->stack_.top_[-1].getString()->c_str();
}


const char *luaO_pushfstring (LuaThread *L, const char *fmt, ...) {
  THREAD_CHECK(L);
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = luaO_pushvfstring(fmt, argp);
  va_end(argp);
  return msg;
}

std::string luaO_chunkid2 (std::string source) {
  int bufflen = LUA_IDSIZE - 1;

  if (source[0] == '=') {
    source.erase(source.begin());
    if(source.size() > LUA_IDSIZE-1) source.resize(LUA_IDSIZE-1);
    return source;
  }

  if (source[0] == '@') {
    source.erase(source.begin());
    if(source.size() <= LUA_IDSIZE - 1) {
      return source;
    }
    else {
      bufflen -= 3;
      source = std::string(source.end() - bufflen, source.end());
      return "..." + source;
    }
  }

  bufflen -= strlen("[string \"...\"]");

  int len = source.size();

  const char *nl = strchr(source.c_str(), '\n');
  if(nl) len = nl - source.c_str();
  std::string line(source.c_str(),len);

  if((nl == NULL) && (line.size() <= (size_t)bufflen)) {
    return "[string \"" + line + "\"]";
  }
  else {
    if(line.size() > (size_t)bufflen) line.resize(bufflen);
    return "[string \"" + line + "...\"]";
  }
}
