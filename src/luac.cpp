/*
** $Id: luac.c,v 1.69 2011/11/29 17:46:33 lhf Exp $
** Lua compiler (saves bytecodes to files; also list bytecodes)
** See Copyright Notice in lua.h
*/

#include "LuaClosure.h"
#include "LuaProto.h"
#include "LuaState.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define luac_c

#include "lua.h"
#include "lauxlib.h"

#include "lobject.h"
#include "lstate.h"
#include "lundump.h"

static void PrintFunction(const LuaProto* f, int full);
#define luaU_print	PrintFunction

#define PROGNAME	"luac"		/* default program name */
#define OUTPUT		PROGNAME ".out"	/* default output file */

static int listing=0;			/* list bytecodes? */
static int dumping=1;			/* dump bytecodes? */
static int stripping=0;			/* strip debug information? */
static char Output[]={ OUTPUT };	/* default output file name */
static const char* output=Output;	/* actual output file name */
static const char* progname=PROGNAME;	/* actual program name */

static void fatal(const char* message)
{
 fprintf(stderr,"%s: %s\n",progname,message);
 exit(EXIT_FAILURE);
}

static void cannot(const char* what)
{
 fprintf(stderr,"%s: cannot %s %s: %s\n",progname,what,output,strerror(errno));
 exit(EXIT_FAILURE);
}

static void usage(const char* message)
{
 if (*message=='-')
  fprintf(stderr,"%s: unrecognized option " LUA_QS "\n",progname,message);
 else
  fprintf(stderr,"%s: %s\n",progname,message);
 fprintf(stderr,
  "usage: %s [options] [filenames]\n"
  "Available options are:\n"
  "  -l       list (use -l -l for full listing)\n"
  "  -o name  output to file " LUA_QL("name") " (default is \"%s\")\n"
  "  -p       parse only\n"
  "  -s       strip debug information\n"
  "  -v       show version information\n"
  "  --       stop handling options\n"
  "  -        stop handling options and process stdin\n"
  ,progname,Output);
 exit(EXIT_FAILURE);
}

#define IS(s)	(strcmp(argv[i],s)==0)

static int doargs(int argc, char* argv[])
{
 int i;
 int version=0;
 if (argv[0]!=NULL && *argv[0]!=0) progname=argv[0];
 for (i=1; i<argc; i++)
 {
  if (*argv[i]!='-')			/* end of options; keep it */
   break;
  else if (IS("--"))			/* end of options; skip it */
  {
   ++i;
   if (version) ++version;
   break;
  }
  else if (IS("-"))			/* end of options; use stdin */
   break;
  else if (IS("-l"))			/* list */
   ++listing;
  else if (IS("-o"))			/* output file */
  {
   output=argv[++i];
   if (output==NULL || *output==0 || (*output=='-' && output[1]!=0))
    usage(LUA_QL("-o") " needs argument");
   if (IS("-")) output=NULL;
  }
  else if (IS("-p"))			/* parse only */
   dumping=0;
  else if (IS("-s"))			/* strip debug information */
   stripping=1;
  else if (IS("-v"))			/* show version */
   ++version;
  else					/* unknown option */
   usage(argv[i]);
 }
 if (i==argc && (listing || !dumping))
 {
  dumping=0;
  argv[--i]=Output;
 }
 if (version)
 {
  printf("%s\n",LUA_COPYRIGHT);
  if (version==argc-1) exit(EXIT_SUCCESS);
 }
 return i;
}

// This compiles a chunk of code consisting of N copies of a dummy function, then
// hacks up the subprotos to point at protos on the stack... *headdesk*
static const LuaProto* combine(LuaThread* L, int n)
{
  if (n==1) {
    return L->stack_.top_[-1].getLClosure()->proto_;
  }
  else
  {
    std::string buffer;
    for(int i = 0; i < n; i++) {
      buffer += "(function()end)();";
    }

    Zio z;
    z.init(buffer.c_str(), buffer.size());
    if (lua_load(L, &z, "=(" PROGNAME ")", NULL) != LUA_OK) {
      fatal(lua_tostring(L,-1));
    }
    LuaProto* f = L->stack_.top_[-1].getLClosure()->proto_;
    for (int i=0; i<n; i++) {
      f->subprotos_[i] = L->stack_.top_[i-n-1].getLClosure()->proto_;
      if (f->subprotos_[i]->upvalues.size()>0) f->subprotos_[i]->upvalues[0].instack = 0;
    }
    f->lineinfo.clear();
    return f;
  }
}

static int writer(LuaThread* L, const void* p, size_t size, void* u)
{
 UNUSED(L);
 return (fwrite(p,size,1,(FILE*)u)!=1) && (size!=0);
}

static int pmain(LuaThread* L)
{
 int argc=(int)lua_tointeger(L,1);
 char** argv=(char**)lua_touserdata(L,2);
 const LuaProto* f;
 int i;
 if (!lua_checkstack(L,argc)) fatal("too many input files");
 for (i=0; i<argc; i++)
 {
  const char* filename=IS("-") ? NULL : argv[i];
  if (luaL_loadfile(L,filename)!=LUA_OK) fatal(lua_tostring(L,-1));
 }
 f=combine(L,argc);
 if (listing) luaU_print(f,listing>1);
 if (dumping)
 {
  FILE* D= (output==NULL) ? stdout : fopen(output,"wb");
  if (D==NULL) cannot("open");
  luaU_dump(L,f,writer,D,stripping);
  if (ferror(D)) cannot("write");
  if (fclose(D)) cannot("close");
 }
 return 0;
}

int main(int argc, char* argv[])
{
  LuaThread* L;
  int i=doargs(argc,argv);
  argc-=i; argv+=i;
  if (argc<=0) usage("no input files given");
  L=luaL_newstate();
  if (L==NULL) fatal("cannot create state: not enough memory");

  L->stack_.push(pmain);

  lua_pushinteger(L,argc);
  L->stack_.push( LuaValue::Pointer(argv) );
  if (lua_pcall(L,2,0,0)!=LUA_OK) fatal(lua_tostring(L,-1));
  lua_close(L);
  return EXIT_SUCCESS;
}

/*
** $Id: print.c,v 1.68 2011/09/30 10:21:20 lhf Exp $
** print bytecodes
** See Copyright Notice in lua.h
*/

#include <ctype.h>
#include <stdio.h>

#define luac_c

#include "ldebug.h"
#include "lobject.h"
#include "lopcodes.h"

#define VOID(p)		((const void*)(p))

static void PrintString(const LuaString* ts)
{
 const char* s=ts->c_str();
 size_t i,n=ts->getLen();
 printf("%c",'"');
 for (i=0; i<n; i++)
 {
  int c=(int)(unsigned char)s[i];
  switch (c)
  {
   case '"':  printf("\\\""); break;
   case '\\': printf("\\\\"); break;
   case '\a': printf("\\a"); break;
   case '\b': printf("\\b"); break;
   case '\f': printf("\\f"); break;
   case '\n': printf("\\n"); break;
   case '\r': printf("\\r"); break;
   case '\t': printf("\\t"); break;
   case '\v': printf("\\v"); break;
   default:	if (isprint(c))
   			printf("%c",c);
		else
			printf("\\%03d",c);
  }
 }
 printf("%c",'"');
}

static void PrintConstant(const LuaProto* f, int i)
{
  LuaValue v = f->constants[i];
  if(v.isNil()) {
    printf("nil");
    return;
  }

  if(v.isBool()) {
    printf(v.getBool() ? "true" : "false");
    return;
  }

  if(v.isNumber()) {
    printf(LUA_NUMBER_FMT,v.getNumber());
    return;
  }

  if(v.isString()) {
    PrintString(v.getString());
    return;
  }

  printf("? type=%d",v.type());
}

#define UPVALNAME(x) ((f->upvalues[x].name) ? f->upvalues[x].name->c_str() : "-")
#define MYK(x)		(-1-(x))

static void PrintCode(const LuaProto* f)
{
 const Instruction* code=&f->instructions_[0];
 int pc,n=(int)f->instructions_.size();
 for (pc=0; pc<n; pc++)
 {
  Instruction i=code[pc];
  OpCode o=GET_OPCODE(i);
  int a=GETARG_A(i);
  int b=GETARG_B(i);
  int c=GETARG_C(i);
  int ax=GETARG_Ax(i);
  int bx=GETARG_Bx(i);
  int sbx=GETARG_sBx(i);
  int line= f->getLine(pc);
  printf("\t%d\t",pc+1);
  if (line>0) printf("[%d]\t",line); else printf("[-]\t");
  printf("%-9s\t",luaP_opnames[o]);
  switch (getOpMode(o))
  {
   case iABC:
    printf("%d",a);
    if (getBMode(o)!=OpArgN) printf(" %d",ISK(b) ? (MYK(INDEXK(b))) : b);
    if (getCMode(o)!=OpArgN) printf(" %d",ISK(c) ? (MYK(INDEXK(c))) : c);
    break;
   case iABx:
    printf("%d",a);
    if (getBMode(o)==OpArgK) printf(" %d",MYK(bx));
    if (getBMode(o)==OpArgU) printf(" %d",bx);
    break;
   case iAsBx:
    printf("%d %d",a,sbx);
    break;
   case iAx:
    printf("%d",MYK(ax));
    break;
  }
  switch (o)
  {
   case OP_LOADK:
    printf("\t; "); PrintConstant(f,bx);
    break;
   case OP_GETUPVAL:
   case OP_SETUPVAL:
    printf("\t; %s",UPVALNAME(b));
    break;
   case OP_GETTABUP:
    printf("\t; %s",UPVALNAME(b));
    if (ISK(c)) { printf(" "); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_SETTABUP:
    printf("\t; %s",UPVALNAME(a));
    if (ISK(b)) { printf(" "); PrintConstant(f,INDEXK(b)); }
    if (ISK(c)) { printf(" "); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_GETTABLE:
   case OP_SELF:
    if (ISK(c)) { printf("\t; "); PrintConstant(f,INDEXK(c)); }
    break;
   case OP_SETTABLE:
   case OP_ADD:
   case OP_SUB:
   case OP_MUL:
   case OP_DIV:
   case OP_POW:
   case OP_EQ:
   case OP_LT:
   case OP_LE:
    if (ISK(b) || ISK(c))
    {
     printf("\t; ");
     if (ISK(b)) PrintConstant(f,INDEXK(b)); else printf("-");
     printf(" ");
     if (ISK(c)) PrintConstant(f,INDEXK(c)); else printf("-");
    }
    break;
   case OP_JMP:
   case OP_FORLOOP:
   case OP_FORPREP:
   case OP_TFORLOOP:
    printf("\t; to %d",sbx+pc+2);
    break;
   case OP_CLOSURE:
    printf("\t; %p",VOID(f->subprotos_[bx]));
    break;
   case OP_SETLIST:
    if (c==0) printf("\t; %d",(int)code[++pc]); else printf("\t; %d",c);
    break;
   case OP_EXTRAARG:
    printf("\t; "); PrintConstant(f,ax);
    break;
   default:
    break;
  }
  printf("\n");
 }
}

#define SS(x)	((x==1)?"":"s")
#define S(x)	(int)(x),SS(x)

static void PrintHeader(const LuaProto* f)
{
 const char* s=f->source ? f->source->c_str() : "=?";
 if (*s=='@' || *s=='=')
  s++;
 else if (*s==LUA_SIGNATURE[0])
  s="(bstring)";
 else
  s="(string)";
 printf("\n%s <%s:%d,%d> (%d instruction%s at %p)\n",
 	(f->linedefined==0)?"main":"function",s,
	f->linedefined,f->lastlinedefined,
	S(f->instructions_.size()),VOID(f));
 printf("%d%s param%s, %d slot%s, %d upvalue%s, ",
	(int)(f->numparams),f->is_vararg?"+":"",SS(f->numparams),
	S(f->maxstacksize),S(f->upvalues.size()));
 printf("%d local%s, %d constant%s, %d function%s\n",
	S(f->locvars.size()),S(f->constants.size()),S(f->subprotos_.size()));
}

static void PrintDebug(const LuaProto* f)
{
 int i,n;
 n=(int)f->constants.size();
 printf("constants (%d) for %p:\n",n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf("\t%d\t",i+1);
  PrintConstant(f,i);
  printf("\n");
 }
 n=(int)f->locvars.size();
 printf("locals (%d) for %p:\n",n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf("\t%d\t%s\t%d\t%d\n",
  i,f->locvars[i].varname->c_str(),f->locvars[i].startpc+1,f->locvars[i].endpc+1);
 }
 n=(int)f->upvalues.size();
 printf("upvalues (%d) for %p:\n",n,VOID(f));
 for (i=0; i<n; i++)
 {
  printf("\t%d\t%s\t%d\t%d\n",
  i,UPVALNAME(i),f->upvalues[i].instack,f->upvalues[i].idx);
 }
}

static void PrintFunction(const LuaProto* f, int full)
{
 int i,n=(int)f->subprotos_.size();
 PrintHeader(f);
 PrintCode(f);
 if (full) PrintDebug(f);
 for (i=0; i<n; i++) PrintFunction(f->subprotos_[i],full);
}
