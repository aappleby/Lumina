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

struct LoadState {
 LuaThread* L;
 Zio* Z;
 std::vector<char> b_;
 const char* name;
};

static LuaResult error3(LoadState* S, const char* why)
{
  luaO_pushfstring(S->L,"%s: %s precompiled chunk",S->name,why);
  return LUA_ERRSYNTAX;
}

#define LoadByte(S)		(uint8_t)LoadChar(S)
#define LoadVar(S,x)		LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S,b,n,size)	LoadMem(S,b,n,size)

static void LoadBlock(LoadState* S, void* b, size_t size)
{
  LuaResult result = LUA_OK;
  if (S->Z->read(b,size)!=0) {
    result = error3(S, "truncated");
    handleResult(result);
  }
}

static void LoadMem(LoadState* S, void* b, int n, size_t size) {
  return LoadBlock(S,b,n*size);
}

static int LoadChar(LoadState* S)
{
  char x;
  LoadVar(S,x);
  return x;
}

static int LoadInt(LoadState* S)
{
  LuaResult result = LUA_OK;
  int x;
  LoadVar(S,x);
  if(x < 0) {
    result = error3(S,"corrupted");
    handleResult(result);
  }
  return x;
}

static double LoadNumber(LoadState* S)
{
  double x;
  LoadVar(S,x);
  return x;
}

static LuaString* LoadString(LoadState* S)
{
  size_t size;
  LoadVar(S,size);
  if (size==0) {
    return NULL;
  }
  else {
    S->b_.resize(size);
    LoadBlock(S,&S->b_[0],size*sizeof(char));
    return thread_G->strings_->Create(&S->b_[0],size-1); /* remove trailing '\0' */
  }
}

static void LoadCode(LoadState* S, LuaProto* f)
{
  int n=LoadInt(S);
  f->instructions_.resize_nocheck(n);
  LoadVector(S,&f->instructions_[0],n,sizeof(Instruction));
}

static LuaProto* LoadFunction(LoadState* S);

static void LoadConstants(LoadState* S, LuaProto* f)
{
  int i,n;
  n=LoadInt(S);
  f->constants.resize_nocheck(n);

  for (i=0; i<n; i++)
  {
    switch(LoadChar(S))
    {
    case LUA_TNIL:
      f->constants[i] = LuaValue::Nil();
      break;
    case LUA_TBOOLEAN:
      f->constants[i] = LoadChar(S) ? true : false;
      break;
    case LUA_TNUMBER:
      f->constants[i] = LoadNumber(S);
      break;
    case LUA_TSTRING:
      f->constants[i] = LoadString(S);
      break;
    default:
      f->constants[i] = LuaValue::Nil();
    }
  }
  n=LoadInt(S);

  f->subprotos_.resize_nocheck(n);

  for (i=0; i<n; i++) f->subprotos_[i]=NULL;
  for (i=0; i<n; i++) f->subprotos_[i]=LoadFunction(S);
}

static void LoadUpvalues(LoadState* S, LuaProto* f)
{
  int n=LoadInt(S);

  f->upvalues.resize_nocheck(n);
  
  for (int i=0; i<n; i++) {
    f->upvalues[i].name=NULL;
  }

  for (int i=0; i<n; i++) {
    f->upvalues[i].instack=LoadByte(S);
    f->upvalues[i].idx=LoadByte(S);
  }
}

static void LoadDebug(LoadState* S, LuaProto* f)
{
  f->source=LoadString(S);
  int n = LoadInt(S);

  f->lineinfo.resize_nocheck(n);

  LoadVector(S,f->lineinfo.begin(),n,sizeof(int));
  n=LoadInt(S);

  f->locvars.resize_nocheck(n);

  for (int i=0; i < n; i++) {
    f->locvars[i].varname=NULL;
  }

  for (int i=0; i < n; i++)
  {
    f->locvars[i].varname=LoadString(S);
    f->locvars[i].startpc=LoadInt(S);
    f->locvars[i].endpc=LoadInt(S);
  }

  n=LoadInt(S);

  for (int i=0; i < n; i++) {
    f->upvalues[i].name=LoadString(S);
  }
}

static LuaProto* LoadFunction(LoadState* S)
{
  LuaProto* f = new LuaProto();
  f->linkGC(getGlobalGCList());
  LuaResult result = S->L->stack_.push_reserve2(LuaValue(f));
  handleResult(result);

  f->linedefined=LoadInt(S);
  f->lastlinedefined=LoadInt(S);
  f->numparams=LoadByte(S);
  f->is_vararg=LoadByte(S) ? true : false;
  f->maxstacksize=LoadByte(S);
  LoadCode(S,f);
  LoadConstants(S,f);
  LoadUpvalues(S,f);
  LoadDebug(S,f);

  // TODO(aappleby): What exactly is getting popped here?
  S->L->stack_.pop();

  return f;
}

/* the code below must be consistent with the code in luaU_header */
#define N0	LUAC_HEADERSIZE
#define N1	(sizeof(LUA_SIGNATURE)-sizeof(char))
#define N2	N1+2
#define N3	N2+6

static LuaResult LoadHeader(LoadState* S)
{
  LuaResult result = LUA_OK;
  uint8_t h[LUAC_HEADERSIZE];
  uint8_t s[LUAC_HEADERSIZE];
  luaU_header(h);
  memcpy(s,h,sizeof(char));			/* first char already read */
  LoadBlock(S,s+sizeof(char),LUAC_HEADERSIZE-sizeof(char));
  if (memcmp(h,s,N0) == 0) {
    return result;
  }
  if (memcmp(h,s,N1) != 0) {
    result = error3(S,"not a");
    if(result != LUA_OK) return result;
  }
  if (memcmp(h,s,N2) != 0) {
    result = error3(S,"version mismatch in");
    if(result != LUA_OK) return result;
  }
  if (memcmp(h,s,N3) != 0) {
    result = error3(S,"incompatible");
    if(result != LUA_OK) return result;
  }
  else {
    result = error3(S,"corrupted");
    if(result != LUA_OK) return result;
  }
  return result;
}

/*
** load precompiled chunk
*/
LuaProto* luaU_undump (LuaThread* L, Zio* Z, const char* name)
{
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  LoadState S;
  if (*name=='@' || *name=='=') {
    S.name=name+1;
  }
  else if (*name==LUA_SIGNATURE[0]) {
    S.name="binary string";
  }
  else {
    S.name=name;
  }
  S.L=L;
  S.Z=Z;
  result = LoadHeader(&S);
  handleResult(result);
  return LoadFunction(&S);
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
