#pragma once

//#include <string>

namespace std {
  template<class _Elem,	class _Traits, class _Ax> class basic_string;
  template<class _Elem> struct char_traits;
  template<class _Ty> class allocator;
  typedef basic_string<char, char_traits<char>, allocator<char> >	string;
};

// Conversions to/from basic types, text formatting, other low-level
// operations that don't depend on anything else Lua-related.

int luaO_int2fb (unsigned int x);
int luaO_fb2int (int x);

std::string luaO_chunkid2 (std::string source);