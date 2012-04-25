/*
** $Id: lparser.h,v 1.69 2011/07/27 18:09:01 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lzio.h"


/*
** Expression descriptor
*/

typedef enum {
  VVOID,	/* no value */
  VNIL,
  VTRUE,
  VFALSE,
  VK,		/* info = index of constant in `k' */
  VKNUM,	/* nval = numerical value */
  VNONRELOC,	/* info = result register */
  VLOCAL,	/* info = local register */
  VUPVAL,       /* info = index of upvalue in 'upvalues' */
  VINDEXED,	/* t = table register/upvalue; idx = index R/K */
  VJMP,		/* info = instruction pc */
  VRELOCABLE,	/* info = instruction pc */
  VCALL,	/* info = instruction pc */
  VVARARG	/* info = instruction pc */
} expkind;


#define vkisvar(k)	(VLOCAL <= (k) && (k) <= VINDEXED)
#define vkisinreg(k)	((k) == VNONRELOC || (k) == VLOCAL)

struct expdesc {
  expkind k;
  int idx;  /* index (R/K) */
  int tr;  /* table (register or upvalue) */
  int vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
  int info;  /* for generic use */
  double nval;  /* for VKNUM */
  int t;  /* patch list of `exit when true' */
  int f;  /* patch list of `exit when false' */
};


/* description of active local variable */
struct Vardesc {
  short idx;  /* variable index in stack */
};


/* description of pending goto statements and label statements */
struct Labeldesc {
  LuaString *name;  /* label identifier */
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  int nactvar;  /* local level where it appears in current block */
};


/* list of labels or gotos */
typedef struct Labellist {
  LuaVector<Labeldesc> arr;
  int n;  /* number of entries in use */
} Labellist;


/* dynamic structures used by the parser */
struct Dyndata {
  struct {  /* list of active local variables */
    LuaVector<Vardesc> arr;
    int n;
  } actvar;
  Labellist gt;  /* list of pending gotos */
  Labellist label;   /* list of active labels */
};


/* control of blocks */
class BlockCnt;  /* defined in lparser.c */
class LexState;

/* state needed to generate code for a given function */
class FuncState {
public:
  LuaProto *f;  /* current function header */

  // This table maps constants to indexes into the constants array, and
  // is used to de-duplicate constants during compilation.
  LuaTable* constant_map;  /* table to find (and reuse) elements in `k' */

  FuncState *prev;  /* enclosing function */
  LexState *ls;  /* lexical state */
  BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to `ncode') */
  int lasttarget;   /* 'label' of last 'jump label' */
  int jpc;  /* list of pending jumps to `pc' */
  int num_constants;  /* number of elements in `k' */
  int num_protos;  /* number of elements in `p' */
  int firstlocal;  /* index of first local var (in Dyndata array) */
  int nlocvars;  /* number of elements in 'f->locvars' */
  int nactvar;  /* number of active local variables */
  int num_upvals;  /* number of upvalues */
  int freereg;  /* first free register */
};


LuaProto *luaY_parser (LuaThread *L, ZIO *z, Mbuffer *buff,
                              Dyndata *dyd, const char *name, int firstchar);


#endif
