/*
** $Id: lundump.c,v 1.71 2011/12/07 10:39:12 lhf Exp $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"

#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lundump.h"
#include "lzio.h"

typedef struct {
 LuaThread* L;
 ZIO* Z;
 Mbuffer* b;
 const char* name;
} LoadState;

static void error(LoadState* S, const char* why)
{
  luaO_pushfstring(S->L,"%s: %s precompiled chunk",S->name,why);
  throwError(LUA_ERRSYNTAX);
}

#define LoadMem(S,b,n,size)	LoadBlock(S,b,(n)*(size))
#define LoadByte(S)		(uint8_t)LoadChar(S)
#define LoadVar(S,x)		LoadMem(S,&x,1,sizeof(x))
#define LoadVector(S,b,n,size)	LoadMem(S,b,n,size)

#if !defined(luai_verifycode)
#define luai_verifycode(L,b,f)	(f)
#endif

static void LoadBlock(LoadState* S, void* b, size_t size)
{
 if (luaZ_read(S->Z,b,size)!=0) error(S,"truncated");
}

static int LoadChar(LoadState* S)
{
 char x;
 LoadVar(S,x);
 return x;
}

static int LoadInt(LoadState* S)
{
 int x;
 LoadVar(S,x);
 if (x<0) error(S,"corrupted");
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
    char* s=luaZ_openspace(S->L,S->b,size);
    LoadBlock(S,s,size*sizeof(char));
    return thread_G->strings_->Create(s,size-1); /* remove trailing '\0' */
  }
}

static void LoadCode(LoadState* S, LuaProto* f)
{
  int n=LoadInt(S);
  f->code.resize_nocheck(n);
  LoadVector(S,&f->code[0],n,sizeof(Instruction));
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

static void LoadHeader(LoadState* S)
{
 uint8_t h[LUAC_HEADERSIZE];
 uint8_t s[LUAC_HEADERSIZE];
 luaU_header(h);
 memcpy(s,h,sizeof(char));			/* first char already read */
 LoadBlock(S,s+sizeof(char),LUAC_HEADERSIZE-sizeof(char));
 if (memcmp(h,s,N0)==0) return;
 if (memcmp(h,s,N1)!=0) error(S,"not a");
 if (memcmp(h,s,N2)!=0) error(S,"version mismatch in");
 if (memcmp(h,s,N3)!=0) error(S,"incompatible"); else error(S,"corrupted");
}

/*
** load precompiled chunk
*/
LuaProto* luaU_undump (LuaThread* L, ZIO* Z, Mbuffer* buff, const char* name)
{
 THREAD_CHECK(L);
 LoadState S;
 if (*name=='@' || *name=='=')
  S.name=name+1;
 else if (*name==LUA_SIGNATURE[0])
  S.name="binary string";
 else
  S.name=name;
 S.L=L;
 S.Z=Z;
 S.b=buff;
 LoadHeader(&S);
 return luai_verifycode(L,buff,LoadFunction(&S));
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
