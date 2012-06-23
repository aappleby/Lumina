// Conversions to/from basic types, text formatting, other low-level
// operations that don't depend on anything else Lua-related.

#pragma once

#include <string>
#include "stdarg.h"

int luaO_int2fb (unsigned int x);
int luaO_fb2int (int x);

std::string luaO_chunkid2 (std::string source);

void StringVprintf(const char *fmt,
                   va_list argp,
                   std::string& result);

std::string StringPrintf(const char* fmt, ...);

int luaO_str2d (const char *s, size_t len, double *result);
int luaO_hexavalue (int c);
