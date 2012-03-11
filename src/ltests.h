/*
** $Id: ltests.h,v 2.33 2010/07/28 15:51:59 roberto Exp $
** Internal Header for Debugging of the Lua Implementation
** See Copyright Notice in lua.h
*/

#ifndef ltests_h
#define ltests_h

/* do not use compatibility macros in Lua code */
#undef LUA_COMPAT_API

#define LUA_DEBUG

void *debug_realloc (void *block, size_t osize, size_t nsize);

int luaB_opentests (lua_State *L);

#endif
