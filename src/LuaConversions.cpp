#include "LuaConversions.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <string>

#include "lctype.h"

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
  return ((e+1) << 3) | ((int)x - 8);
}


/* converts back */
int luaO_fb2int (int x) {
  int e = (x >> 3) & 0x1f;
  if (e == 0) return x;
  else return ((x & 7) + 8) << (e - 1);
}



/*
@@ LUA_IDSIZE gives the maximum size for the description of the source
@* of a function in debug information.
** CHANGE it if you want a different size.
*/
#define LUA_IDSIZE	60

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


bool StringVprintf(const char *fmt, va_list argp, std::string& result, std::string& error) {
  char buff[256];
  int n = 0;
  for (;;) {
    const char *e = strchr(fmt, '%');
    if (e == NULL) break;

    result += std::string(fmt, e-fmt);

    switch (*(e+1)) {
      case 's': {
        const char *s = va_arg(argp, char *);
        if (s == NULL) s = "(null)";
        result += s;
        break;
      }
      case 'c': {
        char buff;
        buff = (char)va_arg(argp, int);
        result += std::string(&buff,1);
        break;
      }
      case 'd': {
        int x = va_arg(argp, int);
        int l = sprintf(buff, "%d", x);
        result += std::string(buff, l);
        break;
      }
      case 'f': {
        double x = va_arg(argp, double);
        int l = sprintf(buff, "%.14g", x);
        result += std::string(buff, l);
        break;
      }
      case 'p': {
        int l = sprintf(buff, "%p", va_arg(argp, void *));
        result += std::string(buff, l);
        break;
      }
      case '%': {
        result += "%";
        break;
      }
      default: {
        int l = sprintf(buff, "invalid option '%%%c' to 'lua_pushfstring'", *(e + 1));
        error = std::string(buff, l);
        return false;
      }
    }
    n += 2;
    fmt = e+2;
  }
  result += fmt;

  return true;
}

bool StringVprintf (const char *fmt, va_list argp, std::string& result) {
  std::string error;
  return StringVprintf(fmt, argp, result, error);
}

std::string StringPrintf(const char* fmt, ...) {
  va_list argp;
  va_start(argp, fmt);

  std::string result;
  std::string error;

  bool ok = StringVprintf(fmt, argp, result, error);
  assert(ok);

  va_end(argp);

  return result;
}



int luaO_hexavalue (int c) {
  if (lisdigit(c)) return c - '0';
  else return ltolower(c) - 'a' + 10;
}


static int isneg (const char **s) {
  if (**s == '-') { (*s)++; return 1; }
  else if (**s == '+') (*s)++;
  return 0;
}


static double readhexa (const char **s, double r, int *count) {
  for (; lisxdigit(unsigned char(**s)); (*s)++) {  /* read integer part */
    r = (r * 16.0) + double(luaO_hexavalue(unsigned char(**s)));
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
  *endptr = (char *)s;  /* nothing is valid yet */
  while (lisspace(unsigned char(*s))) s++;  /* skip initial spaces */
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
  *endptr = (char *)s;  /* valid up to here */
  if (*s == 'p' || *s == 'P') {  /* exponent part? */
    int exp1 = 0;
    int neg1;
    s++;  /* skip 'p' */
    neg1 = isneg(&s);  /* signal */
    if (!lisdigit(unsigned char(*s)))
      goto ret;  /* must have at least one digit */
    while (lisdigit(unsigned char(*s)))  /* read exponent */
      exp1 = exp1 * 10 + *(s++) - '0';
    if (neg1) exp1 = -exp1;
    e += exp1;
  }
  *endptr = (char *)s;  /* valid up to here */
 ret:
  if (neg) r = -r;
  return ldexp(r, e);
}

int luaO_str2d (const char *s, size_t len, double *result) {
  char *endptr;
  if (strpbrk(s, "nN"))  /* reject 'inf' and 'nan' */
    return 0;
  else if (strpbrk(s, "xX"))  /* hexa? */
    *result = lua_strx2number(s, &endptr);
  else
    *result = strtod(s, &endptr);
  if (endptr == s) return 0;  /* nothing recognized */
  while (lisspace(unsigned char(*endptr))) endptr++;
  return (endptr == s + len);  /* OK if no trailing characters */
}
