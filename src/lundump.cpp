/*
** $Id: lundump.c,v 1.71 2011/12/07 10:39:12 lhf Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"

#include <string.h>
#include <vector>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lundump.h"
#include "lzio.h"

static void LoadFunction(Zio* z, LuaProto*& out);

template<class T>
void LoadVector(Zio* z, LuaVector<T>& v) {
  int n = z->read<int>();
  v.resize_nocheck(n);
  z->read(v.begin(), n * sizeof(T));
}

static LuaString* LoadString(Zio* z)
{
  size_t size = z->read<size_t>();
  if (size==0) {
    return NULL;
  }
  else {
    std::vector<char> buf;
    buf.resize(size);
    z->read(&buf[0],size * sizeof(char));
    return thread_G->strings_->Create(&buf[0], size-1); /* remove trailing '\0' */
  }
}

static void LoadConstants(Zio* z, LuaProto* f)
{
  int n = z->read<int>();
  f->constants.resize_nocheck(n);

  for (int i=0; i < n; i++)
  {
    switch(z->read<char>())
    {
    case LUA_TNIL:
      f->constants[i] = LuaValue::Nil();
      break;
    case LUA_TBOOLEAN:
      f->constants[i] = z->read<char>() ? true : false;
      break;
    case LUA_TNUMBER:
      f->constants[i] = z->read<double>();
      break;
    case LUA_TSTRING:
      f->constants[i] = LoadString(z);
      break;
    default:
      f->constants[i] = LuaValue::Nil();
    }
  }

  n = z->read<int>();
  f->subprotos_.resize_nocheck(n);
  for (int i=0; i < n; i++) {
    LoadFunction(z, f->subprotos_[i]);
  }
}

static void LoadUpvalues(Zio* z, LuaProto* f)
{
  int n = z->read<int>();
  f->upvalues.resize_nocheck(n);
  
  for (int i=0; i<n; i++) {
    f->upvalues[i].instack = z->read<uint8_t>();
    f->upvalues[i].idx = z->read<uint8_t>();
  }
}

static void LoadDebug(Zio* z, LuaProto* f)
{
  f->source = LoadString(z);

  LoadVector(z, f->lineinfo);

  int n = z->read<int>();
  f->locvars.resize_nocheck(n);

  for (int i=0; i < n; i++)
  {
    f->locvars[i].varname = LoadString(z);
    f->locvars[i].startpc = z->read<int>();
    f->locvars[i].endpc = z->read<int>();
  }

  n = z->read<int>();
  for (int i=0; i < n; i++) {
    f->upvalues[i].name = LoadString(z);
  }
}

static void LoadFunction(Zio* z, LuaProto*& out)
{
  LuaProto* f = new LuaProto();
  f->linkGC(getGlobalGCList());

  f->linedefined = z->read<int>();
  f->lastlinedefined = z->read<int>();
  
  f->numparams = z->read<uint8_t>();
  f->is_vararg = z->read<uint8_t>() ? true : false;
  f->maxstacksize = z->read<uint8_t>();

  LoadVector(z, f->instructions_);

  LoadConstants(z, f);
  LoadUpvalues(z,f);
  LoadDebug(z,f);

  out = f;
}

/* the code below must be consistent with the code in luaU_header */
#define N0	LUAC_HEADERSIZE
#define N1	(sizeof(LUA_SIGNATURE)-sizeof(char))
#define N2	N1+2
#define N3	N2+6

/*
** load precompiled chunk
*/
LuaResult luaU_undump (LuaThread* L, Zio* Z, const char* name, LuaProto*& out)
{
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  if (*name=='@' || *name=='=') {
    name=name+1;
  }
  else if (*name==LUA_SIGNATURE[0]) {
    name="binary string";
  }
  else {
    name=name;
  }

  uint8_t h[LUAC_HEADERSIZE];
  luaU_header(h);
  
  uint8_t s[LUAC_HEADERSIZE];
  Z->read(s, LUAC_HEADERSIZE);
  
  if (memcmp(h,s,N0) != 0) {
    if (memcmp(h,s,N1) != 0) {
      luaO_pushfstring(L, "%s: not a precompiled chunk",name);
      return LUA_ERRSYNTAX;
    }
    if (memcmp(h,s,N2) != 0) {
      luaO_pushfstring(L, "%s: version mismatch in precompiled chunk",name);
      return LUA_ERRSYNTAX;
    }
    if (memcmp(h,s,N3) != 0) {
      luaO_pushfstring(L, "%s: incompatible precompiled chunk",name);
      return LUA_ERRSYNTAX;
    }
    else {
      luaO_pushfstring(L, "%s: corrupted precompiled chunk",name);
      return LUA_ERRSYNTAX;
    }
  }

  LuaProto* p = NULL;
  LoadFunction(Z, p);

  if (Z->error()) {
    luaO_pushfstring(L, "%s: truncated precompiled chunk",name);
    return LUA_ERRSYNTAX;
  }

  out = p;
  return result;
}

#define MYINT(s)	(s[0]-'0')
#define VERSION		MYINT(LUA_VERSION_MAJOR)*16+MYINT(LUA_VERSION_MINOR)
#define FORMAT		0		/* this is the official format */

/*
* make header for precompiled chunks
* if you change the code below be sure to update LoadHeader and FORMAT above
* and LUAC_HEADERSIZE in lundump.h
*/
void luaU_header (uint8_t* h)
{
  int x=1;
  memcpy(h,LUA_SIGNATURE,sizeof(LUA_SIGNATURE)-sizeof(char));
  h+=sizeof(LUA_SIGNATURE)-sizeof(char);
  *h++=cast_byte(VERSION);
  *h++=cast_byte(FORMAT);
  *h++=cast_byte(*(char*)&x);			/* endianness */
  *h++=cast_byte(sizeof(int));
  *h++=cast_byte(sizeof(size_t));
  *h++=cast_byte(sizeof(Instruction));
  *h++=cast_byte(sizeof(double));
  *h++=cast_byte(((double)0.5)==0);		/* is double integral? */
  memcpy(h,LUAC_TAIL,sizeof(LUAC_TAIL)-sizeof(char));
}
