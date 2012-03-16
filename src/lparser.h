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
  struct {
    struct {  /* for indexed variables (VINDEXED) */
      short idx;  /* index (R/K) */
      uint8_t t;  /* table (register or upvalue) */
      uint8_t vt;  /* whether 't' is register (VLOCAL) or upvalue (VUPVAL) */
    } ind;
    int info;  /* for generic use */
    lua_Number nval;  /* for VKNUM */
  } u;
  int t;  /* patch list of `exit when true' */
  int f;  /* patch list of `exit when false' */
};


/* description of active local variable */
struct Vardesc {
  short idx;  /* variable index in stack */
};


/* description of pending goto statements and label statements */
struct Labeldesc {
  TString *name;  /* label identifier */
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  uint8_t nactvar;  /* local level where it appears in current block */
};


/* list of labels or gotos */
typedef struct Labellist {
  Labeldesc *arr;  /* array */
  int n;  /* number of entries in use */
  int size;  /* array size */
} Labellist;


/* dynamic structures used by the parser */
struct Dyndata {
  struct {  /* list of active local variables */
    Vardesc *arr;
    int n;
    int size;
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
  Proto *f;  /* current function header */
  Table *h;  /* table to find (and reuse) elements in `k' */
  FuncState *prev;  /* enclosing function */
  LexState *ls;  /* lexical state */
  BlockCnt *bl;  /* chain of current blocks */
  int pc;  /* next position to code (equivalent to `ncode') */
  int lasttarget;   /* 'label' of last 'jump label' */
  int jpc;  /* list of pending jumps to `pc' */
  int nk;  /* number of elements in `k' */
  int np;  /* number of elements in `p' */
  int firstlocal;  /* index of first local var (in Dyndata array) */
  short nlocvars;  /* number of elements in 'f->locvars' */
  uint8_t nactvar;  /* number of active local variables */
  uint8_t nups;  /* number of upvalues */
  uint8_t freereg;  /* first free register */
};


Proto *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                              Dyndata *dyd, const char *name, int firstchar);


#endif
