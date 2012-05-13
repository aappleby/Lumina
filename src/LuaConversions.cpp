#include "LuaConversions.h"

#include <string>

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
