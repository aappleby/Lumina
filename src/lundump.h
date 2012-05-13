/*
** $Id: lundump.h,v 1.44 2011/05/06 13:35:17 lhf Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#ifndef lundump_h
#define lundump_h

#include "lobject.h"
#include "lzio.h"

/* load one chunk; from lundump.c */
LuaProto* luaU_undump (LuaThread* L, Zio* Z, const char* name);

/* make header; from lundump.c */
void luaU_header (uint8_t* h);

/* dump one chunk; from ldump.c */
int luaU_dump (LuaThread* L, const LuaProto* f, lua_Writer w, void* data, int strip);

/* data to catch conversion errors */
#define LUAC_TAIL		"\x19\x93\r\n\x1a\n"

/* size in bytes of header of binary files */
#define LUAC_HEADERSIZE		(sizeof(LUA_SIGNATURE)-sizeof(char)+2+6+sizeof(LUAC_TAIL)-sizeof(char))

#endif
