/*
** $Id: lctype.h,v 1.12 2011/07/15 12:50:29 roberto Exp $
** 'ctype' functions for Lua
** See Copyright Notice in lua.h
*/

#ifndef lctype_h
#define lctype_h

bool lislalpha(int c);
bool lislalnum(int c);
bool lisdigit(int c);
bool lisspace(int c);
bool lisprint(int c);
bool lisxdigit(int c);

int ltolower(int c);

#endif

