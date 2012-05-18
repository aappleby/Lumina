/*
** $Id: lparser.c,v 2.124 2011/12/02 13:23:56 roberto Exp $
** Lua Parser
** See Copyright Notice in lua.h
*/

#include "LuaConversions.h"
#include "LuaGlobals.h"
#include "LuaProto.h"
#include "LuaState.h"

#include <string.h>

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"

#pragma warning(disable:4127)

/* maximum number of local variables per function (must be smaller
   than 250, due to the bytecode format) */
#define MAXVARS		200


#define hasmultret(k)		((k) == VCALL || (k) == VVARARG)



/*
** nodes for block list (list of active blocks)
*/
class BlockCnt {
public:
  BlockCnt *previous;  /* chain */
  int firstlabel;  /* index of first label in this block */
  int firstgoto;  /* index of first pending goto in this block */
  int nactvar;  /* # active locals outside the block */
  int upval;  /* true if some variable in the block is an upvalue */
  int isloop;  /* true if `block' is a loop */
};



/*
** prototypes for recursive non-terminal functions
*/
static LuaResult statement (LexState *ls);
static LuaResult expr (LexState *ls, expdesc *v);


/* semantic error */
static LuaResult semerror (LexState *ls, const char *msg) {
  ls->t.token = 0;  /* remove 'near to' from final message */
  return luaX_syntaxerror(ls, msg);
}


static LuaResult error_expected (LexState *ls, int token) {
  std::string text1 = ls->lexer_.getDebugToken(token);
  std::string text2 = StringPrintf("%s expected", text1.c_str());
  return luaX_syntaxerror(ls, text2.c_str());
}


static LuaResult errorlimit (FuncState *fs, int limit, const char *what) {
  LuaThread *L = fs->ls->L;
  const char *msg;
  int line = fs->f->linedefined;
  const char *where = (line == 0)
                      ? "main function"
                      : luaO_pushfstring(L, "function at line %d", line);
  msg = luaO_pushfstring(L, "too many %s (limit is %d) in %s", what, limit, where);
  return luaX_syntaxerror(fs->ls, msg);
}


static LuaResult checklimit (FuncState *fs, int v, int l, const char *what) {
  LuaResult result = LUA_OK;
  if (v > l) {
    return errorlimit(fs, l, what);
  }
  return result;
}


static LuaResult testnext (LexState *ls, int c, int& out) {
  LuaResult result = LUA_OK;
  if (ls->t.token == c) {
    result = luaX_next(ls);
    if(result != LUA_OK) return result;
    out = 1;
  }
  else {
    out = 0;
  }
  return result;
}


static LuaResult check_token (LexState *ls, int c) {
  LuaResult result = LUA_OK;
  if (ls->t.token != c) {
    result = error_expected(ls, c);
  }
  return result;
}


static LuaResult check_next (LexState *ls, int c) {
  LuaResult result = LUA_OK;
  result = check_token(ls, c);
  if(result != LUA_OK) return result;
  return luaX_next(ls);
}


static LuaResult check_condition(LexState* ls, bool c, const char* msg) {
  if(!c) {
    return luaX_syntaxerror(ls, msg);
  }
  return LUA_OK;
}



static LuaResult check_match (LexState *ls, int what, int who, int where) {
  LuaResult result = LUA_OK;
  int temp;
  result = testnext(ls, what, temp);
  if(result != LUA_OK) return result;
  
  if (!temp) {
    if (where == ls->lexer_.getLineNumber()) {
      result = error_expected(ls, what);
      if(result != LUA_OK) return result;
    }
    else {
      std::string what_token = ls->lexer_.getDebugToken(what);
      std::string who_token = ls->lexer_.getDebugToken(who);
      std::string text = StringPrintf("%s expected (to close %s at line %d)", what_token.c_str(), who_token.c_str(), where);
      result = luaX_syntaxerror(ls, text.c_str());
      if(result != LUA_OK) return result;
    }
  }
  return result;
}

/*
** creates a new string and anchors it in function's table so that
** it will not be collected until the end of the function's compilation
** (by that time it should be anchored in function's prototype)
*/
LuaString *luaX_newstring (LexState *ls, const char *str, size_t l) {

  LuaString* ts = thread_G->strings_->Create(str, l);  /* create new string */

  // TODO(aappleby): Save string in 'ls->fs->h'. Why it does so exactly this way, I don't
  // know. Will have to investigate in the future.
  LuaValue s(ts);
  ls->fs->constant_map->set(s, LuaValue(true));

  luaC_barrierback(ls->fs->constant_map, s);

  return ts;
}




static LuaResult str_checkname (LexState *ls, LuaString*& out) {
  LuaResult result = LUA_OK;
  result = check_token(ls, TK_NAME);
  if(result != LUA_OK) return result;

  //LuaString* ts = ls->t.ts;
  LuaString* ts = luaX_newstring(ls, ls->t.c_str(), ls->t.getLen());

  result = luaX_next(ls);
  if(result != LUA_OK) return result;
  out = ts;
  return result;
}


static void init_exp (expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->info = i;
}


static void codestring (LexState *ls, expdesc *e, LuaString *s) {
  init_exp(e, VK, luaK_stringK(ls->fs, s));
}


static LuaResult checkname (LexState *ls, expdesc *e) {
  LuaResult result = LUA_OK;
  LuaString* temp;
  result = str_checkname(ls, temp);
  if(result != LUA_OK) return result;
  codestring(ls, e, temp);
  return result;
}


static int registerlocalvar (LexState *ls, LuaString *varname) {
  FuncState *fs = ls->fs;
  LuaProto *f = fs->f;
  int oldsize = (int)f->locvars.size();
  if(fs->nlocvars >= (int)f->locvars.size()) {
    f->locvars.grow();
  }
  
  while (oldsize < (int)f->locvars.size()) f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->nlocvars].varname = varname;
  luaC_barrier(f, LuaValue(varname));
  return fs->nlocvars++;
}


static LuaResult new_localvar (LexState *ls, LuaString *name) {
  LuaResult result = LUA_OK;
  FuncState *fs = ls->fs;
  Dyndata *dyd = ls->dyd;
  int reg = registerlocalvar(ls, name);
  result = checklimit(fs, dyd->actvar.n + 1 - fs->firstlocal, MAXVARS, "local variables");
  if(result != LUA_OK) return result;
  if(dyd->actvar.n+1 >= (int)dyd->actvar.arr.size()) {
    dyd->actvar.arr.grow();
  }
  dyd->actvar.arr[dyd->actvar.n++].idx = cast(short, reg);
  return result;
}


static LuaResult new_localvarliteral(LexState* ls, const char* name) {
  return new_localvar(ls, luaX_newstring(ls, name, strlen(name)));
}

static LocVar *getlocvar (FuncState *fs, int i) {
  int idx = fs->ls->dyd->actvar.arr[fs->firstlocal + i].idx;
  assert(idx < fs->nlocvars);
  return &fs->f->locvars[idx];
}


static void adjustlocalvars (LexState *ls, int nvars) {
  FuncState *fs = ls->fs;
  fs->nactvar = cast_byte(fs->nactvar + nvars);
  for (; nvars; nvars--) {
    getlocvar(fs, fs->nactvar - nvars)->startpc = fs->pc;
  }
}


static void removevars (FuncState *fs, int tolevel) {
  fs->ls->dyd->actvar.n -= (fs->nactvar - tolevel);
  while (fs->nactvar > tolevel)
    getlocvar(fs, --fs->nactvar)->endpc = fs->pc;
}


static int searchupvalue (FuncState *fs, LuaString *name) {
  int i;
  Upvaldesc *up = fs->f->upvalues.begin();
  for (i = 0; i < fs->num_upvals; i++) {
    if (up[i].name == name) return i;
  }
  return -1;  /* not found */
}


static LuaResult newupvalue (FuncState *fs, LuaString *name, expdesc *v, int& out) {
  LuaResult result = LUA_OK;
  LuaProto *f = fs->f;
  int oldsize = (int)f->upvalues.size();
  result = checklimit(fs, fs->num_upvals + 1, MAXUPVAL, "upvalues");
  if(result != LUA_OK) return result;
  if(fs->num_upvals >= (int)f->upvalues.size()) {
    f->upvalues.grow();
  }
  while (oldsize < (int)f->upvalues.size()) f->upvalues[oldsize++].name = NULL;
  f->upvalues[fs->num_upvals].instack = (v->k == VLOCAL);
  f->upvalues[fs->num_upvals].idx = cast_byte(v->info);
  f->upvalues[fs->num_upvals].name = name;
  luaC_barrier(f, LuaValue(name));
  out = fs->num_upvals++;
  return result;
}


static int searchvar (FuncState *fs, LuaString *n) {
  int i;
  for (i=fs->nactvar-1; i >= 0; i--) {
    if (n == getlocvar(fs, i)->varname)
      return i;
  }
  return -1;  /* not found */
}


/*
  Mark block where variable at given level was defined
  (to emit close instructions later).
*/
static void markupval (FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level) bl = bl->previous;
  bl->upval = 1;
}


/*
  Find variable with given name 'n'. If it is an upvalue, add this
  upvalue into all intermediate functions.
*/
static LuaResult singlevaraux (FuncState *fs, LuaString *n, expdesc *var, int base, int& out) {
  LuaResult result = LUA_OK;
  /* no more levels? */
  if (fs == NULL) {
    out = VVOID;  /* default is global */
    return result;
  }

  /* look up locals at current level */
  int v = searchvar(fs, n);
  if (v >= 0) {  /* found? */
    init_exp(var, VLOCAL, v);  /* variable is local */
    if (!base)
      markupval(fs, v);  /* local will be used as an upval */
    out = VLOCAL;
    return result;
  }

  /* not found as local at current level; try upvalues */
  int idx = searchupvalue(fs, n);  /* try existing upvalues */
  if (idx < 0) {  /* not found? */
    int temp;
    result = singlevaraux(fs->prev, n, var, 0, temp);
    if(result != LUA_OK) return result;
    if (temp == VVOID) {
      /* try upper levels */
      out = VVOID;  /* not found; is a global */
      return result;
    }
    /* else was LOCAL or UPVAL */
    result = newupvalue(fs, n, var, idx);  /* will be a new upvalue */
    if(result != LUA_OK) return result;
  }
  init_exp(var, VUPVAL, idx);
  out = VUPVAL;
  return result;
}


static LuaResult singlevar (LexState *ls, expdesc *var) {
  LuaResult result = LUA_OK;
  LuaString *varname;
  result = str_checkname(ls, varname);
  if(result != LUA_OK) return result;

  FuncState *fs = ls->fs;
  int temp;
  result = singlevaraux(fs, varname, var, 1, temp);
  if(result != LUA_OK) return result;

  if (temp == VVOID) {  /* global name? */
    expdesc key;
    result = singlevaraux(fs, ls->envn, var, 1, temp);  /* get environment variable */
    if(result != LUA_OK) return result;
    assert(var->k == VLOCAL || var->k == VUPVAL);
    codestring(ls, &key, varname);  /* key is variable name */
    luaK_indexed(fs, var, &key);  /* env[varname] */
  }
  return result;
}


static void adjust_assign (LexState *ls, int nvars, int nexps, expdesc *e) {
  FuncState *fs = ls->fs;
  int extra = nvars - nexps;
  if (hasmultret(e->k)) {
    extra++;  /* includes call itself */
    if (extra < 0) extra = 0;
    luaK_setreturns(fs, e, extra);  /* last exp. provides the difference */
    if (extra > 1) luaK_reserveregs(fs, extra-1);
  }
  else {
    if (e->k != VVOID) luaK_exp2nextreg(fs, e);  /* close last expression */
    if (extra > 0) {
      int reg = fs->freereg;
      luaK_reserveregs(fs, extra);
      luaK_nil(fs, reg, extra);
    }
  }
}



static LuaResult closegoto (LexState *ls, int g, Labeldesc *label) {
  LuaResult result = LUA_OK;
  int i;
  FuncState *fs = ls->fs;
  Labellist *gl = &ls->dyd->gt;
  Labeldesc *gt = &gl->arr[g];
  assert(gt->name == label->name);
  if (gt->nactvar < label->nactvar) {
    LuaString *vname = getlocvar(fs, gt->nactvar)->varname;
    const char *msg = luaO_pushfstring(ls->L,
      "<goto %s> at line %d jumps into the scope of local " LUA_QS,
      gt->name->c_str(), gt->line, vname->c_str());
    result = semerror(ls, msg);
    if(result != LUA_OK) return result;
  }
  luaK_patchlist(fs, gt->pc, label->pc);
  /* remove goto from pending list */
  for (i = g; i < gl->n - 1; i++)
    gl->arr[i] = gl->arr[i + 1];
  gl->n--;
  return result;
}


/*
** try to close a goto with existing labels; this solves backward jumps
*/
static LuaResult findlabel (LexState *ls, int g, int& out) {
  LuaResult result = LUA_OK;
  int i;
  BlockCnt *bl = ls->fs->bl;
  Dyndata *dyd = ls->dyd;
  Labeldesc *gt = &dyd->gt.arr[g];
  /* check labels in current block for a match */
  for (i = bl->firstlabel; i < dyd->label.n; i++) {
    Labeldesc *lb = &dyd->label.arr[i];
    if (lb->name == gt->name) {  /* correct label? */
      if (gt->nactvar > lb->nactvar &&
          (bl->upval || dyd->label.n > bl->firstlabel))
        luaK_patchclose(ls->fs, gt->pc, lb->nactvar);
      result = closegoto(ls, g, lb);  /* close it */
      if(result != LUA_OK) return result;
      out = 1;
      return result;
    }
  }
  /* label not found; cannot close goto */
  out = 0;
  return result;
}


static int newlabelentry (LexState *ls, Labellist *l, LuaString *name,
                          int line, int pc) {
  int n = l->n;
  if(n >= (int)l->arr.size()) {
    l->arr.grow();
  }
  l->arr[n].name = name;
  l->arr[n].line = line;
  l->arr[n].nactvar = ls->fs->nactvar;
  l->arr[n].pc = pc;
  l->n++;
  return n;
}


/*
** check whether new label 'lb' matches any pending gotos in current
** block; solves forward jumps
*/
static LuaResult findgotos (LexState *ls, Labeldesc *lb) {
  LuaResult result = LUA_OK;
  Labellist *gl = &ls->dyd->gt;
  int i = ls->fs->bl->firstgoto;
  while (i < gl->n) {
    if (gl->arr[i].name == lb->name) {
      result = closegoto(ls, i, lb);
      if(result != LUA_OK) return result;
    }
    else {
      i++;
    }
  }
  return result;
}


/*
** "export" pending gotos to outer level, to check them against
** outer labels; if the block being exited has upvalues, and
** the goto exits the scope of any variable (which can be the
** upvalue), close those variables being exited.
*/
static LuaResult movegotosout (FuncState *fs, BlockCnt *bl) {
  LuaResult result = LUA_OK;
  int i = bl->firstgoto;
  Labellist *gl = &fs->ls->dyd->gt;
  /* correct pending gotos to current block and try to close it
     with visible labels */
  while (i < gl->n) {
    Labeldesc *gt = &gl->arr[i];
    if (gt->nactvar > bl->nactvar) {
      if (bl->upval)
        luaK_patchclose(fs, gt->pc, bl->nactvar);
      gt->nactvar = bl->nactvar;
    }
    int temp;
    result = findlabel(fs->ls, i, temp);
    if(result != LUA_OK) return result;
    if (!temp) {
      i++;  /* move to next one */
    }
  }
  return result;
}


static void enterblock (FuncState *fs, BlockCnt *bl, uint8_t isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->nactvar;
  bl->firstlabel = fs->ls->dyd->label.n;
  bl->firstgoto = fs->ls->dyd->gt.n;
  bl->upval = 0;
  bl->previous = fs->bl;
  fs->bl = bl;
  assert(fs->freereg == fs->nactvar);
}


/*
** create a label named "break" to resolve break statements
*/
static LuaResult breaklabel (LexState *ls) {
  LuaResult result = LUA_OK;
  LuaString* n = thread_G->strings_->Create("break");
  int l = newlabelentry(ls, &ls->dyd->label, n, 0, ls->fs->pc);
  result = findgotos(ls, &ls->dyd->label.arr[l]);
  return result;
}

/*
** generates an error for an undefined 'goto'; choose appropriate
** message when label name is a reserved word (which can only be 'break')
*/
static LuaResult undefgoto (LexState *ls, Labeldesc *gt) {
  const char* name = gt->name->c_str();
  const char *msg = (strcmp(name, "break") == 0)
                    ? "<%s> at line %d not inside a loop"
                    : "no visible label " LUA_QS " for <goto> at line %d";
  msg = luaO_pushfstring(ls->L, msg, name, gt->line);
  return semerror(ls, msg);
}


static LuaResult leaveblock (FuncState *fs) {
  LuaResult result = LUA_OK;
  BlockCnt *bl = fs->bl;
  LexState *ls = fs->ls;
  if (bl->previous && bl->upval) {
    /* create a 'jump to here' to close upvalues */
    int j = luaK_jump(fs);
    luaK_patchclose(fs, j, bl->nactvar);
    luaK_patchtohere(fs, j);
  }
  if (bl->isloop) {
    result = breaklabel(ls);  /* close pending breaks */
    if(result != LUA_OK) return result;
  }
  fs->bl = bl->previous;
  removevars(fs, bl->nactvar);
  assert(bl->nactvar == fs->nactvar);
  fs->freereg = fs->nactvar;  /* free registers */
  ls->dyd->label.n = bl->firstlabel;  /* remove local labels */
  if (bl->previous) {
    /* inner block? */
    result = movegotosout(fs, bl);  /* update pending gotos to outer block */
    if(result != LUA_OK) return result;
  }
  else if (bl->firstgoto < ls->dyd->gt.n) {
    /* pending gotos in outer block? */
    result = undefgoto(ls, &ls->dyd->gt.arr[bl->firstgoto]);  /* error */
    if(result != LUA_OK) return result;
  }
  return result;
}


/*
** adds prototype being created into its parent list of prototypes
** and codes instruction to create new closure
*/
static void codeclosure (LexState *ls, LuaProto *clp, expdesc *v) {
  FuncState *fs = ls->fs->prev;
  LuaProto *f = fs->f;  /* prototype of function creating new closure */
  if (fs->num_protos >= (int)f->subprotos_.size()) {
    int oldsize = (int)f->subprotos_.size();
    if(fs->num_protos >= (int)f->subprotos_.size()) {
      f->subprotos_.grow();
    }
    while (oldsize < (int)f->subprotos_.size()) f->subprotos_[oldsize++] = NULL;
  }
  f->subprotos_[fs->num_protos++] = clp;
  luaC_barrier(f, LuaValue(clp));
  init_exp(v, VRELOCABLE, luaK_codeABx(fs, OP_CLOSURE, 0, fs->num_protos-1));
  luaK_exp2nextreg(fs, v);  /* fix it at stack top (for GC) */
}


static LuaResult open_func (LexState *ls, FuncState *fs, BlockCnt *bl) {
  LuaResult result = LUA_OK;

  LuaThread *L = ls->L;

  fs->prev = ls->fs;  /* linked list of funcstates */
  fs->ls = ls;
  ls->fs = fs;
  fs->pc = 0;
  fs->lasttarget = 0;
  fs->jpc = NO_JUMP;
  fs->freereg = 0;
  fs->num_constants = 0;
  fs->num_protos = 0;
  fs->num_upvals = 0;
  fs->nlocvars = 0;
  fs->nactvar = 0;
  fs->firstlocal = ls->dyd->actvar.n;
  fs->bl = NULL;

  LuaProto* f = new LuaProto();
  f->linkGC(getGlobalGCList());

  /* anchor prototype (to avoid being collected) */
  result = L->stack_.push_reserve2(LuaValue(f));
  if(result != LUA_OK) return result;

  fs->f = f;
  f->source = ls->L->l_G->strings_->Create(ls->lexer_.getSource());
  f->maxstacksize = 2;  /* registers 0/1 are always valid */

  fs->constant_map = new LuaTable();
  /* anchor table of constants (to avoid being collected) */
  
  result = L->stack_.push_reserve2(LuaValue(fs->constant_map));
  if(result != LUA_OK) return result;

  enterblock(fs, bl, 0);
  return result;
}


static LuaResult close_func (LexState *ls) {
  LuaResult result = LUA_OK;

  LuaThread *L = ls->L;
  FuncState *fs = ls->fs;
  LuaProto *f = fs->f;
  luaK_ret(fs, 0, 0);  /* final return */
  
  result = leaveblock(fs);
  if(result != LUA_OK) return result;

  f->instructions_.resize_nocheck(fs->pc);
  f->lineinfo.resize_nocheck(fs->pc);
  f->constants.resize_nocheck(fs->num_constants);
  f->subprotos_.resize_nocheck(fs->num_protos);
  f->locvars.resize_nocheck(fs->nlocvars);
  f->upvalues.resize_nocheck(fs->num_upvals);

  assert(fs->bl == NULL);
  ls->fs = fs->prev;
  L->stack_.pop();  /* pop table of constants */
  L->stack_.pop();  /* pop prototype (after possible collection) */

  return result;
}


/*
** opens the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
static LuaResult open_mainfunc (LexState *ls, FuncState *fs, BlockCnt *bl) {
  LuaResult result = LUA_OK;
  expdesc v;
  result = open_func(ls, fs, bl);
  if(result != LUA_OK) return result;
  fs->f->is_vararg = true;  /* main function is always vararg */
  init_exp(&v, VLOCAL, 0);
  int temp;
  result = newupvalue(fs, ls->envn, &v, temp);  /* create environment upvalue */
  return result;
}



/*============================================================*/
/* GRAMMAR RULES */
/*============================================================*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it handled in separate.
*/
static int block_follow (LexState *ls, int withuntil) {
  switch (ls->t.token) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END: case TK_EOS:
      return 1;
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


static LuaResult statlist (LexState *ls) {
  LuaResult result = LUA_OK;
  /* statlist -> { stat [`;'] } */
  while (!block_follow(ls, 1)) {
    if (ls->t.token == TK_RETURN) {
      result = statement(ls);
      return result;  /* 'return' must be last statement */
    }
    result = statement(ls);
    if(result != LUA_OK) return result;
  }
  return result;
}


static LuaResult fieldsel (LexState *ls, expdesc *v) {
  LuaResult result = LUA_OK;
  /* fieldsel -> ['.' | ':'] NAME */
  FuncState *fs = ls->fs;
  expdesc key;
  luaK_exp2anyregup(fs, v);
  
  result = luaX_next(ls);  /* skip the dot or colon */
  if(result != LUA_OK) return result;

  result = checkname(ls, &key);
  if(result != LUA_OK) return result;

  luaK_indexed(fs, v, &key);
  return result;
}


static LuaResult yindex (LexState *ls, expdesc *v) {
  LuaResult result = LUA_OK;
  /* index -> '[' expr ']' */
  result = luaX_next(ls);  /* skip the '[' */
  if(result != LUA_OK) return result;

  result = expr(ls, v);
  if(result != LUA_OK) return result;
  
  luaK_exp2val(ls->fs, v);
  
  result = check_next(ls, ']');
  return result;
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/


struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of `record' elements */
  int na;  /* total number of array elements */
  int tostore;  /* number of array elements pending to be stored */
};


static LuaResult recfield (LexState *ls, struct ConsControl *cc) {
  LuaResult result = LUA_OK;
  /* recfield -> (NAME | `['exp1`]') = exp1 */
  FuncState *fs = ls->fs;
  int reg = ls->fs->freereg;
  expdesc key, val;
  int rkkey;
  if (ls->t.token == TK_NAME) {
    result = checklimit(fs, cc->nh, MAX_INT, "items in a constructor");
    if(result != LUA_OK) return result;
    result = checkname(ls, &key);
    if(result != LUA_OK) return result;
  }
  else {
    /* ls->t.token == '[' */
    result = yindex(ls, &key);
    if(result != LUA_OK) return result;
  }
  cc->nh++;
  result = check_next(ls, '=');
  if(result != LUA_OK) return result;
  rkkey = luaK_exp2RK(fs, &key);
  result = expr(ls, &val);
  if(result != LUA_OK) return result;
  luaK_codeABC(fs, OP_SETTABLE, cc->t->info, rkkey, luaK_exp2RK(fs, &val));
  fs->freereg = reg;  /* free registers */
  return result;
}


static void closelistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->v.k == VVOID) return;  /* there is no list item */
  luaK_exp2nextreg(fs, &cc->v);
  cc->v.k = VVOID;
  if (cc->tostore == LFIELDS_PER_FLUSH) {
    luaK_setlist(fs, cc->t->info, cc->na, cc->tostore);  /* flush */
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, struct ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.k)) {
    luaK_setmultret(fs, &cc->v);
    luaK_setlist(fs, cc->t->info, cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.k != VVOID)
      luaK_exp2nextreg(fs, &cc->v);
    luaK_setlist(fs, cc->t->info, cc->na, cc->tostore);
  }
}


static LuaResult listfield (LexState *ls, struct ConsControl *cc) {
  LuaResult result = LUA_OK;
  /* listfield -> exp */
  result = expr(ls, &cc->v);
  if(result != LUA_OK) return result;
  result = checklimit(ls->fs, cc->na, MAX_INT, "items in a constructor");
  if(result != LUA_OK) return result;
  cc->na++;
  cc->tostore++;
  return result;
}


static LuaResult field2 (LexState *ls, struct ConsControl *cc) {
  LuaResult result = LUA_OK;
  /* field -> listfield | recfield */
  switch(ls->t.token) {
    case TK_NAME: {  /* may be 'listfield' or 'recfield' */
      int temp;
      result = luaX_lookahead(ls, temp);
      if(result != LUA_OK) return result;
      // expression?
      if (temp != '=') {
        result = listfield(ls, cc);
        if(result != LUA_OK) return result;
      }
      else {
        result = recfield(ls, cc);
        if(result != LUA_OK) return result;
      }
      break;
    }
    case '[': {
      result = recfield(ls, cc);
      if(result != LUA_OK) return result;
      break;
    }
    default: {
      result = listfield(ls, cc);
      if(result != LUA_OK) return result;
      break;
    }
  }
  return result;
}


static LuaResult constructor2 (LexState *ls, expdesc *t) {
  LuaResult result = LUA_OK;
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *fs = ls->fs;
  int line = ls->lexer_.getLineNumber();
  int pc = luaK_codeABC(fs, OP_NEWTABLE, 0, 0, 0);
  struct ConsControl cc;
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  init_exp(t, VRELOCABLE, pc);
  init_exp(&cc.v, VVOID, 0);  /* no value (yet) */
  luaK_exp2nextreg(ls->fs, t);  /* fix it at stack top */

  result = check_next(ls, '{');
  if(result != LUA_OK) return result;

  int temp1, temp2;
  do {
    assert(cc.v.k == VVOID || cc.tostore > 0);
    if (ls->t.token == '}') break;
    closelistfield(fs, &cc);
    result = field2(ls, &cc);
    if(result != LUA_OK) return result;
    result = testnext(ls, ',', temp1);
    if(result != LUA_OK) return result;
    result = testnext(ls, ';', temp2);
    if(result != LUA_OK) return result;
  } while (temp1 || temp2);
  
  result = check_match(ls, '}', '{', line);
  if(result != LUA_OK) return result;

  lastlistfield(fs, &cc);
  SETARG_B(fs->f->instructions_[pc], luaO_int2fb(cc.na)); /* set initial array size */
  SETARG_C(fs->f->instructions_[pc], luaO_int2fb(cc.nh));  /* set initial table size */
  return result;
}

/* }====================================================================== */



static LuaResult parlist (LexState *ls) {
  LuaResult result = LUA_OK;
  /* parlist -> [ param { `,' param } ] */
  FuncState *fs = ls->fs;
  LuaProto *f = fs->f;
  int nparams = 0;
  f->is_vararg = false;
  if (ls->t.token != ')') {  /* is `parlist' not empty? */
    int temp;
    do {
      switch (ls->t.token) {
        case TK_NAME: {  /* param -> NAME */
          LuaString* temp;
          result = str_checkname(ls, temp);
          if(result != LUA_OK) return result;

          result = new_localvar(ls, temp);
          if(result != LUA_OK) return result;
          nparams++;
          break;
        }
        case TK_DOTS: {  /* param -> `...' */
          result = luaX_next(ls);
          if(result != LUA_OK) return result;
          f->is_vararg = true;
          break;
        }
        default: {
          result = luaX_syntaxerror(ls, "<name> or " LUA_QL("...") " expected");
          if(result != LUA_OK) return result;
        }
      }
      result = testnext(ls, ',', temp);
      if(result != LUA_OK) return result;
    } while (!f->is_vararg && temp);
  }
  adjustlocalvars(ls, nparams);
  f->numparams = cast_byte(fs->nactvar);
  luaK_reserveregs(fs, fs->nactvar);  /* reserve register for parameters */
  return result;
}


static LuaResult body2 (LexState *ls, expdesc *e, int ismethod, int line) {
  LuaResult result = LUA_OK;
  /* body ->  `(' parlist `)' block END */
  FuncState new_fs;
  BlockCnt bl;
  result = open_func(ls, &new_fs, &bl);
  if(result != LUA_OK) return result;

  new_fs.f->linedefined = line;

  result = check_next(ls, '(');
  if(result != LUA_OK) return result;

  if (ismethod) {
    result = new_localvarliteral(ls, "self");  /* create 'self' parameter */
    if(result != LUA_OK) return result;
    adjustlocalvars(ls, 1);
  }

  result = parlist(ls);
  if(result != LUA_OK) return result;
  
  result = check_next(ls, ')');
  if(result != LUA_OK) return result;
  
  result = statlist(ls);
  if(result != LUA_OK) return result;

  new_fs.f->lastlinedefined = ls->lexer_.getLineNumber();
  
  result = check_match(ls, TK_END, TK_FUNCTION, line);
  if(result != LUA_OK) return result;

  codeclosure(ls, new_fs.f, e);
  result = close_func(ls);
  return result;
}


static LuaResult explist (LexState *ls, expdesc *v, int& out) {
  LuaResult result = LUA_OK;
  /* explist -> expr { `,' expr } */
  int n = 1;  /* at least one expression */
  result = expr(ls, v);
  if(result != LUA_OK) return result;

  int temp;
  while (1) {
    result = testnext(ls, ',', temp);
    if(result != LUA_OK) return result;
    if(!temp) break;
    luaK_exp2nextreg(ls->fs, v);
    result = expr(ls, v);
    if(result != LUA_OK) return result;
    n++;
  }
  out = n;
  return result;
}


static LuaResult funcargs2 (LexState *ls, expdesc *f, int line) {
  LuaResult result = LUA_OK;
  FuncState *fs = ls->fs;
  expdesc args;
  memset(&args,0,sizeof(args));
  int base, nparams;
  switch (ls->t.token) {
    case '(': {  /* funcargs -> `(' [ explist ] `)' */
      result = luaX_next(ls);
      if(result != LUA_OK) return result;

      if (ls->t.token == ')')  /* arg list is empty? */
        args.k = VVOID;
      else {
        int temp;
        result = explist(ls, &args, temp);
        if(result != LUA_OK) return result;
        luaK_setmultret(fs, &args);
      }
      result = check_match(ls, ')', '(', line);
      if(result != LUA_OK) return result;
      break;
    }
    case '{': {  /* funcargs -> constructor */
      result = constructor2(ls, &args);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_STRING: {  /* funcargs -> STRING */
      LuaString* ts = luaX_newstring(ls, ls->t.c_str(), ls->t.getLen());
      codestring(ls, &args, ts);
      result = luaX_next(ls);  /* must use `seminfo' before `next' */
      if(result != LUA_OK) return result;
      break;
    }
    default: {
      result = luaX_syntaxerror(ls, "function arguments expected");
      if(result != LUA_OK) return result;
    }
  }
  assert(f->k == VNONRELOC);
  base = f->info;  /* base register for call */
  if (hasmultret(args.k))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.k != VVOID)
      luaK_exp2nextreg(fs, &args);  /* close last argument */
    nparams = fs->freereg - (base+1);
  }
  init_exp(f, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams+1, 2));
  luaK_fixline(fs, line);
  fs->freereg = base+1;  /* call remove function and arguments and leaves
                            (unless changed) one result */
  return result;
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


// prefixexp -> NAME | '(' expr ')'
static LuaResult prefixexp (LexState *ls, expdesc *v) {
  LuaResult result = LUA_OK;
  switch (ls->t.token) {
    case '(': {
      int line = ls->lexer_.getLineNumber();
      result = luaX_next(ls);
      if(result != LUA_OK) return result;

      result = expr(ls, v);
      if(result != LUA_OK) return result;
      
      result = check_match(ls, ')', '(', line);
      if(result != LUA_OK) return result;

      luaK_dischargevars(ls->fs, v);
      return result;
    }
    case TK_NAME: {
      return singlevar(ls, v);
    }
    default: {
      result = luaX_syntaxerror(ls, "unexpected symbol");
      if(result != LUA_OK) return result;
    }
  }
  return result;
}


// primaryexp -> prefixexp { `.' NAME | `[' exp `]' | `:' NAME funcargs | funcargs }
static LuaResult primaryexp (LexState *ls, expdesc *v) {
  LuaResult result = LUA_OK;
  FuncState *fs = ls->fs;
  int line = ls->lexer_.getLineNumber();
  result = prefixexp(ls, v);
  if(result != LUA_OK) return result;
  for (;;) {
    switch (ls->t.token) {
      case '.': {  /* fieldsel */
        result = fieldsel(ls, v);
        if(result != LUA_OK) return result;
        break;
      }
      case '[': {  /* `[' exp1 `]' */
        expdesc key;
        luaK_exp2anyregup(fs, v);
        result = yindex(ls, &key);
        if(result != LUA_OK) return result;
        luaK_indexed(fs, v, &key);
        break;
      }
      case ':': {  /* `:' NAME funcargs */
        expdesc key;
        result = luaX_next(ls);
        if(result != LUA_OK) return result;

        result = checkname(ls, &key);
        if(result != LUA_OK) return result;

        luaK_self(fs, v, &key);
        result = funcargs2(ls, v, line);
        if(result != LUA_OK) return result;
        break;
      }
      case '(': case TK_STRING: case '{': {  /* funcargs */
        luaK_exp2nextreg(fs, v);
        result = funcargs2(ls, v, line);
        if(result != LUA_OK) return result;
        break;
      }
      default: return result;
    }
  }
}


// simpleexp -> NUMBER | STRING | NIL | TRUE | FALSE | ... | constructor | FUNCTION body | primaryexp
static LuaResult simpleexp (LexState *ls, expdesc *v) {
  LuaResult result = LUA_OK;
  switch (ls->t.token) {
    case TK_NUMBER: {
      init_exp(v, VKNUM, 0);
      v->nval = ls->t.r;
      break;
    }
    case TK_STRING: {
      LuaString* ts = luaX_newstring(ls, ls->t.c_str(), ls->t.getLen());
      codestring(ls, v, ts);
      break;
    }
    case TK_NIL: {
      init_exp(v, VNIL, 0);
      break;
    }
    case TK_TRUE: {
      init_exp(v, VTRUE, 0);
      break;
    }
    case TK_FALSE: {
      init_exp(v, VFALSE, 0);
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *fs = ls->fs;
      result = check_condition(ls, fs->f->is_vararg, "cannot use " LUA_QL("...") " outside a vararg function");
      if(result != LUA_OK) return result;
      init_exp(v, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, 1, 0));
      break;
    }
    case '{': {  /* constructor */
      result = constructor2(ls, v);
      return result;
    }
    case TK_FUNCTION: {
      result = luaX_next(ls);
      if(result != LUA_OK) return result;
      return body2(ls, v, 0, ls->lexer_.getLineNumber());
    }
    default: {
      return primaryexp(ls, v);
    }
  }
  return luaX_next(ls);
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '/': return OPR_DIV;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}


static const struct {
  uint8_t left;  /* left priority for each binary operator */
  uint8_t right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {6, 6}, {6, 6}, {7, 7}, {7, 7}, {7, 7},  /* `+' `-' `*' `/' `%' */
   {10, 9}, {5, 4},                 /* ^, .. (right associative) */
   {3, 3}, {3, 3}, {3, 3},          /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},          /* ~=, >, >= */
   {2, 2}, {1, 1}                   /* and, or */
};

#define UNARY_PRIORITY	8  /* priority for unary operators */


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where `binop' is any binary operator with a priority higher than `limit'
*/
static LuaResult subexpr (LexState *ls, expdesc *v, int limit, BinOpr& out) {
  LuaResult result = LUA_OK;
  ScopedCallDepth d(ls->L);

  if (ls->L->l_G->call_depth_ > LUAI_MAXCCALLS) {
    return errorlimit(ls->fs, LUAI_MAXCCALLS, "C levels");
  }

  UnOpr uop = getunopr(ls->t.token);
  if (uop != OPR_NOUNOPR) {
    int line = ls->lexer_.getLineNumber();
    result = luaX_next(ls);
    if(result != LUA_OK) return result;
    BinOpr temp;
    result = subexpr(ls, v, UNARY_PRIORITY, temp);
    if(result != LUA_OK) return result;
    luaK_prefix(ls->fs, uop, v, line);
  }
  else {
    LuaResult result = simpleexp(ls, v);
    if(result != LUA_OK) return result;
  }

  /* expand while operators have priorities higher than `limit' */
  BinOpr op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->lexer_.getLineNumber();
    result = luaX_next(ls);
    if(result != LUA_OK) return result;
    luaK_infix(ls->fs, op, v);
    /* read sub-expression with higher priority */
    result = subexpr(ls, &v2, priority[op].right, nextop);
    if(result != LUA_OK) return result;
    luaK_posfix(ls->fs, op, v, &v2, line);
    op = nextop;
  }

  /* return first untreated operator */
  out = op;
  return result;
}


static LuaResult expr (LexState *ls, expdesc *v) {
  BinOpr temp;
  return subexpr(ls, v, 0, temp);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static LuaResult block2 (LexState *ls) {
  LuaResult result = LUA_OK;
  /* block -> statlist */
  FuncState *fs = ls->fs;
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  
  result = statlist(ls);
  if(result != LUA_OK) return result;

  result = leaveblock(fs);
  return result;
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->fs;
  int extra = fs->freereg;  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {  /* check all previous assignments */
    if (lh->v.k == VINDEXED) {  /* assigning to a table? */
      /* table is the upvalue/local being assigned now? */
      if (lh->v.vt == v->k && lh->v.tr == v->info) {
        conflict = 1;
        lh->v.vt = VLOCAL;
        lh->v.tr = extra;  /* previous assignment will use safe copy */
      }
      /* index is the local being assigned? (index cannot be upvalue) */
      if (v->k == VLOCAL && lh->v.idx == v->info) {
        conflict = 1;
        lh->v.idx = extra;  /* previous assignment will use safe copy */
      }
    }
  }
  if (conflict) {
    /* copy upvalue/local value to a temporary (in position 'extra') */
    OpCode op = (v->k == VLOCAL) ? OP_MOVE : OP_GETUPVAL;
    luaK_codeABC(fs, op, extra, v->info, 0);
    luaK_reserveregs(fs, 1);
  }
}


static LuaResult assignment2 (LexState *ls, struct LHS_assign *lh, int nvars) {
  LuaResult result = LUA_OK;
  expdesc e;

  result = check_condition(ls, vkisvar(lh->v.k), "syntax error");
  if(result != LUA_OK) return result;

  int temp;
  result = testnext(ls, ',', temp);
  if(result != LUA_OK) return result;

  if (temp) {  /* assignment -> `,' primaryexp assignment */
    struct LHS_assign nv;
    nv.prev = lh;
    result = primaryexp(ls, &nv.v);
    if(result != LUA_OK) return result;
    if (nv.v.k != VINDEXED)
      check_conflict(ls, lh, &nv.v);

    // Because this operates recursively, having the left hand side of an expression be
    // "a,a,a,a,a,.......,a,a = 100" with too many A's could overflow the stack
    if ((nvars + ls->L->l_G->call_depth_) > LUAI_MAXCCALLS) {
      return errorlimit(ls->fs, LUAI_MAXCCALLS, "C levels");
    }

    result = assignment2(ls, &nv, nvars+1);
    if(result != LUA_OK) return result;
  }
  else {  /* assignment -> `=' explist */
    int nexps;
    
    result = check_next(ls, '=');
    if(result != LUA_OK) return result;

    result = explist(ls, &e, nexps);
    if(result != LUA_OK) return result;

    if (nexps != nvars) {
      adjust_assign(ls, nvars, nexps, &e);
      if (nexps > nvars)
        ls->fs->freereg -= nexps - nvars;  /* remove extra values */
    }
    else {
      luaK_setoneret(ls->fs, &e);  /* close last expression */
      luaK_storevar(ls->fs, &lh->v, &e);
      return result;  /* avoid default */
    }
  }
  init_exp(&e, VNONRELOC, ls->fs->freereg-1);  /* default assignment */
  luaK_storevar(ls->fs, &lh->v, &e);
  return result;
}


static LuaResult cond2 (LexState *ls, int& out) {
  LuaResult result = LUA_OK;
  /* cond -> exp */
  expdesc v;
  result = expr(ls, &v);  /* read condition */
  if(result != LUA_OK) return result;
  if (v.k == VNIL) v.k = VFALSE;  /* `falses' are all equal here */
  luaK_goiftrue(ls->fs, &v);
  out = v.f;
  return result;
}


static LuaResult gotostat (LexState *ls, int pc) {
  LuaResult result = LUA_OK;
  int line = ls->lexer_.getLineNumber();
  LuaString *label;
  int g;
  int temp;
  result = testnext(ls, TK_GOTO, temp);
  if(result != LUA_OK) return result;
  if (temp) {
    result = str_checkname(ls, label);
    if(result != LUA_OK) return result;
  }
  else {
    result = luaX_next(ls);  /* skip break */
    if(result != LUA_OK) return result;
    label = thread_G->strings_->Create("break");
  }
  g = newlabelentry(ls, &ls->dyd->gt, label, line, pc);
  result = findlabel(ls, g, temp);  /* close it if label already defined */
  return result;
}


/* check for repeated labels on the same block */
static LuaResult checkrepeated (FuncState *fs, Labellist *ll, LuaString *label) {
  LuaResult result = LUA_OK;
  int i;
  for (i = fs->bl->firstlabel; i < ll->n; i++) {
    if (label == ll->arr[i].name) {
      const char *msg = luaO_pushfstring(fs->ls->L,
                          "label " LUA_QS " already defined on line %d",
                          label->c_str(), ll->arr[i].line);
      result = semerror(fs->ls, msg);
      if(result != LUA_OK) return result;
    }
  }
  return result;
}


static LuaResult labelstat (LexState *ls, LuaString *label, int line) {
  LuaResult result = LUA_OK;
  /* label -> '::' NAME '::' */
  FuncState *fs = ls->fs;
  Labellist *ll = &ls->dyd->label;
  int l;  /* index of new label being created */
  result = checkrepeated(fs, ll, label);  /* check for repeated labels */
  if(result != LUA_OK) return result;
  result = check_next(ls, TK_DBCOLON);  /* skip double colon */
  if(result != LUA_OK) return result;
  /* create new entry for this label */
  l = newlabelentry(ls, ll, label, line, fs->pc);
  /* skip other no-op statements */
  while (ls->t.token == ';' || ls->t.token == TK_DBCOLON) {
    result = statement(ls);
    if(result != LUA_OK) return result;
  }
  if (block_follow(ls, 0)) {  /* label is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    ll->arr[l].nactvar = fs->bl->nactvar;
  }
  result = findgotos(ls, &ll->arr[l]);
  return result;
}


static LuaResult whilestat (LexState *ls, int line) {
  LuaResult result = LUA_OK;
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  result = luaX_next(ls);  /* skip WHILE */
  if(result != LUA_OK) return result;

  whileinit = luaK_getlabel(fs);
  result = cond2(ls, condexit);
  if(result != LUA_OK) return result;
  enterblock(fs, &bl, 1);
  
  result = check_next(ls, TK_DO);
  if(result != LUA_OK) return result;
  
  result = block2(ls);
  if(result != LUA_OK) return result;

  luaK_jumpto(fs, whileinit);
  result = check_match(ls, TK_END, TK_WHILE, line);
  if(result != LUA_OK) return result;
  result = leaveblock(fs);
  if(result != LUA_OK) return result;
  luaK_patchtohere(fs, condexit);  /* false conditions finish the loop */
  return result;
}


static LuaResult repeatstat (LexState *ls, int line) {
  LuaResult result = LUA_OK;
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->fs;
  int repeat_init = luaK_getlabel(fs);
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  result = luaX_next(ls);  /* skip REPEAT */
  if(result != LUA_OK) return result;
  
  result = statlist(ls);
  if(result != LUA_OK) return result;

  result = check_match(ls, TK_UNTIL, TK_REPEAT, line);
  if(result != LUA_OK) return result;
  result = cond2(ls, condexit);  /* read condition (inside scope block) */
  if(result != LUA_OK) return result;
  if (bl2.upval)  /* upvalues? */
    luaK_patchclose(fs, condexit, bl2.nactvar);
  result = leaveblock(fs);  /* finish scope */
  if(result != LUA_OK) return result;
  luaK_patchlist(fs, condexit, repeat_init);  /* close the loop */
  result = leaveblock(fs);  /* finish loop */
  return result;
}


static LuaResult exp1 (LexState *ls, int& out) {
  LuaResult result = LUA_OK;
  expdesc e;
  result = expr(ls, &e);
  if(result != LUA_OK) return result;
  luaK_exp2nextreg(ls->fs, &e);
  assert(e.k == VNONRELOC);
  out = e.info;
  return result;
}


static LuaResult forbody (LexState *ls, int base, int line, int nvars, int isnum) {
  LuaResult result = LUA_OK;
  /* forbody -> DO block */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  int prep, endfor;
  adjustlocalvars(ls, 3);  /* control variables */

  result = check_next(ls, TK_DO);
  if(result != LUA_OK) return result;

  prep = isnum ? luaK_codeAsBx(fs, OP_FORPREP, base, NO_JUMP) : luaK_jump(fs);
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  adjustlocalvars(ls, nvars);
  luaK_reserveregs(fs, nvars);
  result = block2(ls);
  if(result != LUA_OK) return result;

  result = leaveblock(fs);  /* end of scope for declared variables */
  if(result != LUA_OK) return result;
  luaK_patchtohere(fs, prep);
  if (isnum)  /* numeric for? */
    endfor = luaK_codeAsBx(fs, OP_FORLOOP, base, NO_JUMP);
  else {  /* generic for */
    luaK_codeABC(fs, OP_TFORCALL, base, 0, nvars);
    luaK_fixline(fs, line);
    endfor = luaK_codeAsBx(fs, OP_TFORLOOP, base + 2, NO_JUMP);
  }
  luaK_patchlist(fs, endfor, prep + 1);
  luaK_fixline(fs, line);
  return result;
}


static LuaResult fornum (LexState *ls, LuaString *varname, int line) {
  LuaResult result = LUA_OK;
  /* fornum -> NAME = exp1,exp1[,exp1] forbody */
  FuncState *fs = ls->fs;
  int base = fs->freereg;

  result = new_localvarliteral(ls, "(for index)");
  if(result != LUA_OK) return result;

  result = new_localvarliteral(ls, "(for limit)");
  if(result != LUA_OK) return result;

  result = new_localvarliteral(ls, "(for step)");
  if(result != LUA_OK) return result;
  
  result = new_localvar(ls, varname);
  if(result != LUA_OK) return result;

  result = check_next(ls, '=');
  if(result != LUA_OK) return result;

  int temp;
  result = exp1(ls, temp);  /* initial value */
  if(result != LUA_OK) return result;

  result = check_next(ls, ',');
  if(result != LUA_OK) return result;
  
  result = exp1(ls, temp);  /* limit */
  if(result != LUA_OK) return result;

  result = testnext(ls, ',', temp);
  if(result != LUA_OK) return result;

  if (temp) {
    result = exp1(ls, temp);  /* optional step */
    if(result != LUA_OK) return result;
  }
  else {  /* default step = 1 */
    luaK_codek(fs, fs->freereg, luaK_numberK(fs, 1));
    luaK_reserveregs(fs, 1);
  }
  result = forbody(ls, base, line, 1, 1);
  return result;
}


static LuaResult forlist (LexState *ls, LuaString *indexname) {
  LuaResult result = LUA_OK;

  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *fs = ls->fs;
  expdesc e;
  int nvars = 4;  /* gen, state, control, plus at least one declared var */
  int line;
  int base = fs->freereg;

  /* create control variables */
  result = new_localvarliteral(ls, "(for generator)");
  if(result != LUA_OK) return result;

  result = new_localvarliteral(ls, "(for state)");
  if(result != LUA_OK) return result;

  result = new_localvarliteral(ls, "(for control)");
  if(result != LUA_OK) return result;

  /* create declared variables */
  result = new_localvar(ls, indexname);
  if(result != LUA_OK) return result;

  int temp;
  while (1) {
    result = testnext(ls, ',', temp);
    if(result != LUA_OK) return result;
    if(!temp) break;
    LuaString* temp;
    result = str_checkname(ls, temp);
    if(result != LUA_OK) return result;
    result = new_localvar(ls, temp);
    if(result != LUA_OK) return result;
    nvars++;
  }

  result = check_next(ls, TK_IN);
  if(result != LUA_OK) return result;

  line = ls->lexer_.getLineNumber();

  result = explist(ls, &e, temp);
  if(result != LUA_OK) return result;

  adjust_assign(ls, 3, temp, &e);

  luaK_checkstack(fs, 3);  /* extra space to call generator */
  result = forbody(ls, base, line, nvars - 3, 0);
  return result;
}


static LuaResult forstat (LexState *ls, int line) {
  LuaResult result = LUA_OK;
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->fs;
  LuaString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  
  result = luaX_next(ls);  /* skip `for' */
  if(result != LUA_OK) return result;

  result = str_checkname(ls, varname);  /* first variable name */
  if(result != LUA_OK) return result;
  switch (ls->t.token) {
    case '=': {
      result = fornum(ls, varname, line);
      if(result != LUA_OK) return result;
      break;
    }
    case ',': case TK_IN: {
      result = forlist(ls, varname);
      if(result != LUA_OK) return result;
      break;
    }
    default: {
      result = luaX_syntaxerror(ls, LUA_QL("=") " or " LUA_QL("in") " expected");
      if(result != LUA_OK) return result;
    }
  }
  result = check_match(ls, TK_END, TK_FOR, line);
  if(result != LUA_OK) return result;

  result = leaveblock(fs);  /* loop scope (`break' jumps to this point) */
  return result;
}


static LuaResult test_then_block (LexState *ls, int *escapelist) {
  LuaResult result = LUA_OK;

  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  BlockCnt bl;
  FuncState *fs = ls->fs;
  expdesc v;
  int jf;  /* instruction to skip 'then' code (if condition is false) */
  
  result = luaX_next(ls);  /* skip IF or ELSEIF */
  if(result != LUA_OK) return result;

  result = expr(ls, &v);  /* read condition */
  if(result != LUA_OK) return result;
  
  result = check_next(ls, TK_THEN);
  if(result != LUA_OK) return result;
  
  if (ls->t.token == TK_GOTO || ls->t.token == TK_BREAK) {
    luaK_goiffalse(ls->fs, &v);  /* will jump to label if condition is true */
    enterblock(fs, &bl, 0);  /* must enter block before 'goto' */
    result = gotostat(ls, v.t);  /* handle goto/break */
    if(result != LUA_OK) return result;
    if (block_follow(ls, 0)) {  /* 'goto' is the entire block? */
      result = leaveblock(fs);
      return result;
    }
    else  /* must skip over 'then' part if condition is false */
      jf = luaK_jump(fs);
  }
  else {  /* regular case (not goto/break) */
    luaK_goiftrue(ls->fs, &v);  /* skip over block if condition is false */
    enterblock(fs, &bl, 0);
    jf = v.f;
  }
  result = statlist(ls);  /* `then' part */
  if(result != LUA_OK) return result;

  result = leaveblock(fs);
  if(result != LUA_OK) return result;
  if (ls->t.token == TK_ELSE ||
      ls->t.token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    luaK_concat(fs, escapelist, luaK_jump(fs));  /* must jump over it */
  luaK_patchtohere(fs, jf);
  return result;
}


static LuaResult ifstat (LexState *ls, int line) {
  LuaResult result = LUA_OK;
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *fs = ls->fs;
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  result = test_then_block(ls, &escapelist);  /* IF cond THEN block */
  if(result != LUA_OK) return result;
  while (ls->t.token == TK_ELSEIF) {
    result = test_then_block(ls, &escapelist);  /* ELSEIF cond THEN block */
    if(result != LUA_OK) return result;
  }
  int temp;
  result = testnext(ls, TK_ELSE, temp);
  if(result != LUA_OK) return result;
  if (temp) {
    result = block2(ls);  /* `else' part */
    if(result != LUA_OK) return result;
  }
  result = check_match(ls, TK_END, TK_IF, line);
  if(result != LUA_OK) return result;

  luaK_patchtohere(fs, escapelist);  /* patch escape list to 'if' end */
  return result;
}


static LuaResult localfunc (LexState *ls) {
  LuaResult result = LUA_OK;
  expdesc b;
  FuncState *fs = ls->fs;
  LuaString* temp;
  result = str_checkname(ls, temp);
  if(result != LUA_OK) return result;
  result = new_localvar(ls, temp);  /* new local variable */
  if(result != LUA_OK) return result;
  adjustlocalvars(ls, 1);  /* enter its scope */
  result = body2(ls, &b, 0, ls->lexer_.getLineNumber());  /* function created in next register */
  if(result != LUA_OK) return result;
  /* debug information will only see the variable after this point! */
  getlocvar(fs, b.info)->startpc = fs->pc;
  return result;
}


static LuaResult localstat (LexState *ls) {
  LuaResult result = LUA_OK;
  /* stat -> LOCAL NAME {`,' NAME} [`=' explist] */
  int nvars = 0;
  int nexps;
  expdesc e;
  
  int temp1;
  do {
    LuaString* temp;
    result = str_checkname(ls, temp);
    if(result != LUA_OK) return result;
    result = new_localvar(ls, temp);
    if(result != LUA_OK) return result;
    nvars++;
    result = testnext(ls, ',', temp1);
    if(result != LUA_OK) return result;
  } while (temp1);

  result = testnext(ls, '=', temp1);
  if(result != LUA_OK) return result;
  if (temp1) {
    result = explist(ls, &e, nexps);
    if(result != LUA_OK) return result;
  }
  else {
    e.k = VVOID;
    nexps = 0;
  }
  adjust_assign(ls, nvars, nexps, &e);
  adjustlocalvars(ls, nvars);
  return result;
}


static LuaResult funcname (LexState *ls, expdesc *v, int& out) {
  LuaResult result = LUA_OK;
  /* funcname -> NAME {fieldsel} [`:' NAME] */
  int ismethod = 0;
  result = singlevar(ls, v);
  if(result != LUA_OK) return result;
  while (ls->t.token == '.') {
    result = fieldsel(ls, v);
    if(result != LUA_OK) return result;
  }
  if (ls->t.token == ':') {
    ismethod = 1;
    result = fieldsel(ls, v);
    if(result != LUA_OK) return result;
  }
  out = ismethod;
  return result;
}


static LuaResult funcstat (LexState *ls, int line) {
  LuaResult result = LUA_OK;
  /* funcstat -> FUNCTION funcname body */
  int ismethod;
  expdesc v, b;
  result = luaX_next(ls);  /* skip FUNCTION */
  if(result != LUA_OK) return result;
  
  result = funcname(ls, &v, ismethod);
  if(result != LUA_OK) return result;
  
  result = body2(ls, &b, ismethod, line);
  if(result != LUA_OK) return result;
  
  luaK_storevar(ls->fs, &v, &b);
  luaK_fixline(ls->fs, line);  /* definition `happens' in the first line */
  return result;
}


static LuaResult exprstat (LexState *ls) {
  LuaResult result = LUA_OK;
  /* stat -> func | assignment */
  FuncState *fs = ls->fs;
  struct LHS_assign v;
  result = primaryexp(ls, &v.v);
  if(result != LUA_OK) return result;
  if (v.v.k == VCALL) {  /* stat -> func */
    SETARG_C(getcode(fs, &v.v), 1);  /* call statement uses no results */
    return LUA_OK;
  }
  else {  /* stat -> assignment */
    v.prev = NULL;
    return assignment2(ls, &v, 1);
  }
}


static LuaResult retstat (LexState *ls) {
  LuaResult result = LUA_OK;
  /* stat -> RETURN [explist] [';'] */
  FuncState *fs = ls->fs;
  expdesc e;
  int first, nret;  /* registers with returned values */
  if (block_follow(ls, 1) || ls->t.token == ';') {
    first = nret = 0;  /* return no values */
  }
  else {
    result = explist(ls, &e, nret);  /* optional return values */
    if(result != LUA_OK) return result;

    if (hasmultret(e.k)) {
      luaK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1) {  /* tail call? */
        SET_OPCODE(getcode(fs,&e), OP_TAILCALL);
        assert(GETARG_A(getcode(fs,&e)) == fs->nactvar);
      }
      first = fs->nactvar;
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1) {
        /* only one single value? */
        first = luaK_exp2anyreg(fs, &e);
      }
      else {
        luaK_exp2nextreg(fs, &e);  /* values must go to the `stack' */
        first = fs->nactvar;  /* return all `active' values */
        assert(nret == fs->freereg - first);
      }
    }
  }
  luaK_ret(fs, first, nret);
  int temp;
  result = testnext(ls, ';', temp);  /* skip optional semicolon */
  return result;
}


static LuaResult statement (LexState *ls) {
  LuaResult result = LUA_OK;
  ScopedCallDepth d(ls->L);

  if (ls->L->l_G->call_depth_ > LUAI_MAXCCALLS) {
    return errorlimit(ls->fs, LUAI_MAXCCALLS, "C levels");
  }

  int line = ls->lexer_.getLineNumber();  /* may be needed for error messages */

  switch (ls->t.token) {
    case ';': {  /* stat -> ';' (empty statement) */
      result = luaX_next(ls);  /* skip ';' */
      if(result != LUA_OK) return result;
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      result = ifstat(ls, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      result = whilestat(ls, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      result = luaX_next(ls);  /* skip DO */
      if(result != LUA_OK) return result;
      
      result = block2(ls);
      if(result != LUA_OK) return result;

      result = check_match(ls, TK_END, TK_DO, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      result = forstat(ls, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      result = repeatstat(ls, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_FUNCTION: {  /* stat -> funcstat */
      result = funcstat(ls, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      result = luaX_next(ls);  /* skip LOCAL */
      if(result != LUA_OK) return result;

      int temp;
      result = testnext(ls, TK_FUNCTION, temp);
      if(result != LUA_OK) return result;
      if (temp) {  /* local function? */
        result = localfunc(ls);
        if(result != LUA_OK) return result;
      }
      else {
        result = localstat(ls);
        if(result != LUA_OK) return result;
      }
      break;
    }
    case TK_DBCOLON: {  /* stat -> label */
      result = luaX_next(ls);  /* skip double colon */
      if(result != LUA_OK) return result;

      LuaString* temp;
      result = str_checkname(ls, temp);
      if(result != LUA_OK) return result;
      result = labelstat(ls, temp, line);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      result = luaX_next(ls);  /* skip RETURN */
      if(result != LUA_OK) return result;
      result = retstat(ls);
      if(result != LUA_OK) return result;
      break;
    }
    case TK_BREAK:   /* stat -> breakstat */
    case TK_GOTO: {  /* stat -> 'goto' NAME */
      result = gotostat(ls, luaK_jump(ls->fs));
      if(result != LUA_OK) return result;
      break;
    }
    default: {  /* stat -> func | assignment */
      result = exprstat(ls);
      if(result != LUA_OK) return result;
      break;
    }
  }
  assert(ls->fs->f->maxstacksize >= ls->fs->freereg &&
             ls->fs->freereg >= ls->fs->nactvar);
  ls->fs->freereg = ls->fs->nactvar;  /* free registers */
  return result;
}

/* }====================================================================== */


LuaResult luaY_parser (LuaThread *L,
                       Zio *z,
                       Dyndata *dyd, 
                       const char *name, 
                       LuaProto*& out) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(L);
  LexState lexstate;
  FuncState funcstate;
  BlockCnt bl;
  LuaString* tname = NULL;

  tname = thread_G->strings_->Create(name);
  /* push name to protect it */
  result = L->stack_.push_reserve2(LuaValue(tname));
  if(result != LUA_OK) return result;

  lexstate.dyd = dyd;
  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;
  luaX_setinput(L, &lexstate, z, tname);
  
  result = open_mainfunc(&lexstate, &funcstate, &bl);
  if(result != LUA_OK) return result;

  result = luaX_next(&lexstate);  /* read first token */
  if(result != LUA_OK) return result;
  
  result = statlist(&lexstate);  /* main body */
  if(result != LUA_OK) return result;
  
  result = check_token(&lexstate, TK_EOS);
  if(result != LUA_OK) return result;

  result = close_func(&lexstate);
  if(result != LUA_OK) return result;

  L->stack_.pop();  /* pop name */
  assert(!funcstate.prev && funcstate.num_upvals == 1 && !lexstate.fs);
  /* all scopes should be correctly finished */
  assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);
  out = funcstate.f;
  return result;
}

