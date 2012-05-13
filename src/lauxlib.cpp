/*
** $Id: lauxlib.c,v 1.240 2011/12/06 16:33:55 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

/* This file uses only the official API of Lua.
** Any function declared here could be written as an application function.
*/

#define lauxlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lmem.h"

#include "lstate.h" // for THREAD_CHECK

LuaValue *index2addr (LuaThread *L, int idx);

const char* luaL_typename(LuaThread* L, int index) {
  THREAD_CHECK(L);
  LuaValue v = L->stack_.at(index);
  return v.typeName();
}

/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	12	/* size of the first part of the stack */
#define LEVELS2	10	/* size of the second part of the stack */

void findfield2(LuaThread* L, LuaTable* table1, LuaValue goal, int depth, std::string& result) {

  LuaValue key = table1->findKeyString(goal);

  if(!key.isNone()) {
    result = key.getString()->c_str();
    return;
  }

  if(depth == 0) return;

  int size1 = table1->getTableIndexSize();
  
  for(int i = 0; i < size1; i++) {
    LuaValue key1, val1;
    table1->tableIndexToKeyVal(i, key1, val1);
    if(!key1.isString()) continue;
    if(!val1.isTable()) continue;

    findfield2(L, val1.getTable(), goal, depth-1, result);

    if(result.size()) {
      std::string prefix = key1.getString()->c_str();
      result = prefix + "." + result;
      return;
    }
  }
}

LuaString* getglobalfuncname (LuaThread *L, LuaDebug *ar) {
  THREAD_CHECK(L);

  LuaValue f = *ar->i_ci->getFunc();
  std::string result;
  findfield2(L, L->l_G->getGlobals(), f, 1, result);

  if(result.size()) {
    LuaString* s = L->l_G->strings_->Create(result.c_str());
    return s;
  }
  else {
    return NULL;
  }
}


static void pushfuncname (LuaThread *L, LuaDebug *ar) {
  THREAD_CHECK(L);
  //if (*ar->namewhat != '\0') {
  if(!ar->namewhat2.empty()) {
    /* is there a name? */
    lua_pushfstring(L, "function " LUA_QS, ar->name2.c_str());
  }
  else if (ar->what2 == "main") {
    /* main? */
    lua_pushfstring(L, "main chunk");
  }
  else if (ar->what2 == "C") {
    LuaString* name = getglobalfuncname(L,ar);
    if (name) {
      lua_pushfstring(L, "function " LUA_QS, name->c_str());
    }
    else {
      lua_pushliteral(L, "?");
    }
  }
  else {
    lua_pushfstring(L, "function <%s:%d>", ar->short_src2.c_str(), ar->linedefined);
  }
}


static int countlevels (LuaThread *L) {
  THREAD_CHECK(L);
  LuaDebug ar;
  int li = 1, le = 1;
  /* find an upper bound */
  while (lua_getstack(L, le, &ar)) { li = le; le *= 2; }
  /* do a binary search */
  while (li < le) {
    int m = (li + le)/2;
    if (lua_getstack(L, m, &ar)) li = m + 1;
    else le = m;
  }
  return le - 1;
}


void luaL_traceback (LuaThread *L, LuaThread *L1,
                                const char *msg, int level) {
  THREAD_CHECK(L);
  LuaDebug ar;
  int top = L->stack_.getTopIndex();
  int numlevels;
  {
    THREAD_CHANGE(L1);
    numlevels = countlevels(L1);
  }
  int mark = (numlevels > LEVELS1 + LEVELS2) ? LEVELS1 : 0;
  if (msg) {
    lua_pushfstring(L, "%s\n", msg);
  }
  lua_pushliteral(L, "stack traceback:");
  {
    THREAD_CHANGE(L1);
    while (lua_getstack(L1, level++, &ar)) {
      if (level == mark) {  /* too many levels? */
        {
          THREAD_CHANGE(L);
          lua_pushliteral(L, "\n\t...");  /* add a '...' */
        }
        level = numlevels - LEVELS2;  /* and skip to last ones */
      }
      else {
        lua_getinfo(L1, "Slnt", &ar);
        {
          THREAD_CHANGE(L);
          lua_pushfstring(L, "\n\t%s:", ar.short_src2.c_str());
          if (ar.currentline > 0) {
            lua_pushfstring(L, "%d:", ar.currentline);
          }
          lua_pushliteral(L, " in ");
          pushfuncname(L, &ar);
          if (ar.istailcall) {
            lua_pushliteral(L, "\n\t(...tail calls...)");
          }
          lua_concat(L, L->stack_.getTopIndex() - top);
        }
      }
    }
  }
  lua_concat(L, L->stack_.getTopIndex() - top);
}

/* }====================================================== */


/*
** {======================================================
** Error-report functions
** =======================================================
*/

int luaL_argerror (LuaThread *L, int narg, const char *extramsg) {
  THREAD_CHECK(L);
  LuaDebug ar;
  if (!lua_getstack(L, 0, &ar))  /* no stack frame? */
    return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
  lua_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat2.c_str(), "method") == 0) {
    narg--;  /* do not count `self' */
    if (narg == 0)  /* error is in the self argument itself? */
      return luaL_error(L, "calling " LUA_QS " on bad self", ar.name2.c_str());
  }
  if (ar.name2.empty()) {
    LuaString* name = getglobalfuncname(L, &ar);
    ar.name2 = name ? name->c_str() : "?";
  }
  return luaL_error(L, "bad argument #%d to " LUA_QS " (%s)",
                        narg, ar.name2.c_str(), extramsg);
}


int typeerror (LuaThread *L, int narg, const char* type1) {
  THREAD_CHECK(L);
  const char* type2 = luaL_typename(L, narg);
  const char *msg = lua_pushfstring(L, "%s expected, got %s", type1, type2);
  return luaL_argerror(L, narg, msg);
}


static void tag_error (LuaThread *L, int narg, int tag) {
  THREAD_CHECK(L);
  typeerror(L, narg, lua_typename(L, tag));
}


void luaL_where (LuaThread *L, int level) {
  THREAD_CHECK(L);
  LuaDebug ar;
  if (lua_getstack(L, level, &ar)) {  /* check function at level */
    lua_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      lua_pushfstring(L, "%s:%d: ", ar.short_src2.c_str(), ar.currentline);
      return;
    }
  }
  lua_pushliteral(L, "");  /* else, no information available... */
}


int luaL_error (LuaThread *L, const char *fmt, ...) {
  THREAD_CHECK(L);
  va_list argp;
  va_start(argp, fmt);
  luaL_where(L, 1);
  lua_pushvfstring(L, fmt, argp);
  va_end(argp);
  lua_concat(L, 2);
  return lua_error(L);
}


int luaL_fileresult (LuaThread *L, int stat, const char *fname) {
  THREAD_CHECK(L);
  int en = errno;  /* calls to Lua API may change this value */
  if (stat) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    L->stack_.push(LuaValue::Nil());
    if (fname) {
      lua_pushfstring(L, "%s: %s", fname, strerror(en));
    }
    else {
      lua_pushfstring(L, "%s", strerror(en));
    }
    lua_pushinteger(L, en);
    return 3;
  }
}


int luaL_execresult (LuaThread *L, int stat) {
  THREAD_CHECK(L);
  const char *what = "exit";  /* type of termination */
  if (stat == -1)  /* error? */
    return luaL_fileresult(L, 0, NULL);
  else {
    if (*what == 'e' && stat == 0) {
      // successful termination?
      lua_pushboolean(L, 1);
    }
    else {
      L->stack_.push(LuaValue::Nil());
    }
    lua_pushstring(L, what);
    lua_pushinteger(L, stat);
    return 3;  /* return true/nil,what,code */
  }
}

/* }====================================================== */


/*
** {======================================================
** Userdata's metatable manipulation
** =======================================================
*/

LuaValue luaL_getmetatable2(LuaThread* L, const char* tname) {
  LuaTable* registry = L->l_G->getRegistry();

  LuaValue v = registry->get(tname);
  if(v.isNone()) v = LuaValue::Nil();

  return v;
}

void luaL_getmetatable(LuaThread* L, const char* tname) {
  L->stack_.push( luaL_getmetatable2(L, tname) );
}

void *luaL_testudata (LuaThread *L, int ud, const char *tname) {
  THREAD_CHECK(L);
  LuaValue ud2 = L->stack_.at(ud);
  if(!ud2.isBlob()) return NULL;

  LuaTable* meta1 = ud2.getBlob()->metatable_;
  if(meta1 == NULL) return NULL;

  LuaTable* meta2 = L->l_G->getRegistryTable(tname);
  if (meta1 != meta2) return NULL;

  return ud2.getBlob()->buf_;
}


void *luaL_checkudata (LuaThread *L, int ud, const char *tname) {
  THREAD_CHECK(L);
  void *p = luaL_testudata(L, ud, tname);
  if (p == NULL) typeerror(L, ud, tname);
  return p;
}

/* }====================================================== */


/*
** {======================================================
** Argument check functions
** =======================================================
*/

int luaL_checkoption (LuaThread *L,
                      int narg,
                      const char *def,
                      const char *const lst[]) {
  THREAD_CHECK(L);
  const char *name = (def) ? luaL_optstring(L, narg, def) :
                             luaL_checkstring(L, narg);
  int i;
  for (i=0; lst[i]; i++)
    if (strcmp(lst[i], name) == 0)
      return i;
  const char* text = lua_pushfstring(L, "invalid option " LUA_QS, name);
  return luaL_argerror(L, narg, text);
}


void luaL_checkstack (LuaThread *L, int space, const char *msg) {
  THREAD_CHECK(L);
  /* keep some extra space to run error routines, if needed */
  if (!lua_checkstack(L, space + LUA_MINSTACK)) {
    if (msg)
      luaL_error(L, "stack overflow (%s)", msg);
    else
      luaL_error(L, "stack overflow");
  }
}

void luaL_checkIsFunction(LuaThread *L, int narg) {
  THREAD_CHECK(L);
  LuaValue* pv = index2addr(L, narg);
  assert(pv);
  LuaValue v = *pv;
  if(v.isFunction()) return;
  const char* actualType = v.typeName();
  const char *msg = lua_pushfstring(L, "Expected a function, got a %s", actualType);
  luaL_argerror(L, narg, msg);
}

void luaL_checkIsTable(LuaThread* L, int narg) {
  THREAD_CHECK(L);
  LuaValue v = *index2addr(L, narg);
  if(v.isTable()) return;
  const char* actualType = v.typeName();
  const char *msg = lua_pushfstring(L, "Expected a table, got a %s", actualType);
  luaL_argerror(L, narg, msg);
}

void luaL_checktype (LuaThread *L, int narg, int t) {
  THREAD_CHECK(L);
  if (lua_type(L, narg) != t)
    tag_error(L, narg, t);
}


void luaL_checkany (LuaThread *L, int narg) {
  THREAD_CHECK(L);
  LuaValue* v = index2addr2(L, narg);
  if(v == NULL) {
    luaL_argerror(L, narg, "value expected");
  }
}


const char *luaL_checklstring (LuaThread *L, int narg, size_t *len) {
  THREAD_CHECK(L);
  const char *s = lua_tolstring(L, narg, len);
  if (!s) tag_error(L, narg, LUA_TSTRING);
  return s;
}


const char *luaL_optlstring (LuaThread *L,
                             int narg,
                             const char * default_string,
                             size_t *len) {
  THREAD_CHECK(L);
  if (lua_isnoneornil(L, narg)) {
    if (len)
      *len = (default_string ? strlen(default_string) : 0);
    return default_string;
  }
  else return luaL_checklstring(L, narg, len);
}


double luaL_checknumber (LuaThread *L, int narg) {
  THREAD_CHECK(L);
  LuaValue v1 = L->stack_.at(narg);
  LuaValue v2 = v1.convertToNumber();
  if(v2.isNone()) {
    tag_error(L, narg, LUA_TNUMBER);
  }
  return v2.getNumber();
}


double luaL_optnumber (LuaThread *L, int narg, double def) {
  THREAD_CHECK(L);
  return luaL_opt(L, luaL_checknumber, narg, def);
}


ptrdiff_t luaL_checkinteger (LuaThread *L, int narg) {
  THREAD_CHECK(L);
  int isnum;
  ptrdiff_t d = lua_tointegerx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


uint32_t luaL_checkunsigned (LuaThread *L, int narg) {
  THREAD_CHECK(L);
  int isnum;
  uint32_t d = lua_tounsignedx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


ptrdiff_t luaL_optinteger (LuaThread *L, int narg, ptrdiff_t def) {
  THREAD_CHECK(L);
  return luaL_opt(L, luaL_checkinteger, narg, def);
}


uint32_t luaL_optunsigned (LuaThread *L, int narg, uint32_t def) {
  THREAD_CHECK(L);
  return luaL_opt(L, luaL_checkunsigned, narg, def);
}

/* }====================================================== */


/*
** {======================================================
** Generic Buffer manipulation
** =======================================================
*/

/*
** check whether buffer is using a userdata on the stack as a temporary
** buffer
*/
#define buffonstack(B)	((B)->b != (B)->initb)


/*
** returns a pointer to a free area with at least 'sz' bytes
*/
char *luaL_prepbuffsize (luaL_Buffer *B, size_t sz) {
  LuaThread *L = B->L;
  if (B->size - B->n < sz) {  /* not enough space? */
    char *newbuff;
    size_t newsize = B->size * 2;  /* double buffer size */
    if (newsize - B->n < sz)  /* not bit enough? */
      newsize = B->n + sz;
    if (newsize < B->n || newsize - B->n < sz)
      luaL_error(L, "buffer too large");
    /* create larger buffer */
    newbuff = (char *)lua_newuserdata(L, newsize * sizeof(char));
    /* move content to new buffer */
    memcpy(newbuff, B->b, B->n * sizeof(char));
    if (buffonstack(B))
      L->stack_.remove(-2);  /* remove old buffer */
    B->b = newbuff;
    B->size = newsize;
  }
  return &B->b[B->n];
}


void luaL_addlstring (luaL_Buffer *B, const char *s, size_t l) {
  char *b = luaL_prepbuffsize(B, l);
  memcpy(b, s, l * sizeof(char));
  luaL_addsize(B, l);
}


void luaL_addstring (luaL_Buffer *B, const char *s) {
  luaL_addlstring(B, s, strlen(s));
}


void luaL_pushresult (luaL_Buffer *B) {
  LuaThread *L = B->L;
  lua_pushlstring(L, B->b, B->n);
  if (buffonstack(B))
    L->stack_.remove(-2);  /* remove old buffer */
}


void luaL_pushresultsize (luaL_Buffer *B, size_t sz) {
  luaL_addsize(B, sz);
  luaL_pushresult(B);
}


void luaL_addvalue (luaL_Buffer *B) {
  LuaThread *L = B->L;
  size_t l;
  const char *s = lua_tolstring(L, -1, &l);
  if (buffonstack(B))
    lua_insert(L, -2);  /* put value below buffer */
  luaL_addlstring(B, s, l);
  L->stack_.remove((buffonstack(B)) ? -2 : -1);  /* remove value */
}


void luaL_buffinit (LuaThread *L, luaL_Buffer *B) {
  THREAD_CHECK(L);
  B->L = L;
  B->b = B->initb;
  B->n = 0;
  B->size = LUAL_BUFFERSIZE;
}


char *luaL_buffinitsize (LuaThread *L, luaL_Buffer *B, size_t sz) {
  THREAD_CHECK(L);
  luaL_buffinit(L, B);
  return luaL_prepbuffsize(B, sz);
}

/* }====================================================== */


/*
** {======================================================
** Reference system
** =======================================================
*/

/* index of free-list header */
#define freelist	0

int luaL_ref(LuaThread* L) {

  LuaTable* registry = L->l_G->getRegistry();

  LuaValue val = L->stack_.top_[-1];
  L->stack_.pop();

  if(val.isNil()) return LUA_REFNIL;

  LuaValue ref1 = registry->get(LuaValue(freelist));
  int ref = ref1.isInteger() ? ref1.getInteger() : 0;

  if (ref != 0) {  // any free element?
    LuaValue temp = registry->get( LuaValue(ref) );
    registry->set( LuaValue(freelist), temp );
    registry->set( LuaValue(ref), val );
    return ref;
  }
  else  {
    // no free elements, get a new reference.
    ref = (int)registry->getLength() + 1;
    registry->set( LuaValue(ref), val );
    return ref;
  }
}

void luaL_unref(LuaThread* L, int ref) {
  THREAD_CHECK(L);

  if(ref >= 0) {
    LuaTable* registry = L->l_G->getRegistry();
    LuaValue a = registry->get( LuaValue(freelist) );
    registry->set( LuaValue(ref), a );
    registry->set( LuaValue(freelist), LuaValue(ref) );
  }
}

/* }====================================================== */


/*
** {======================================================
** Load functions
** =======================================================
*/

typedef struct LoadF {
  int n;  /* number of pre-read characters */
  FILE *f;  /* file being read */
  char buff[LUAL_BUFFERSIZE];  /* area for reading file */
} LoadF;


static const char *getF (LuaThread *L, void *ud, size_t *size) {
  THREAD_CHECK(L);
  LoadF *lf = (LoadF *)ud;
  (void)L;  /* not used */
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  }
  else {  /* read a block from file */
    /* 'fread' can return > 0 *and* set the EOF flag. If next call to
       'getF' called 'fread', it might still wait for user input.
       The next check avoids this problem. */
    if (feof(lf->f)) return NULL;
    *size = fread(lf->buff, 1, sizeof(lf->buff), lf->f);  /* read block */
  }
  return lf->buff;
}


static int errfile (LuaThread *L, const char *what, int fnameindex) {
  THREAD_CHECK(L);
  const char *serr = strerror(errno);
  const char *filename = lua_tostring(L, fnameindex) + 1;
  lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
  L->stack_.remove(fnameindex);
  return LUA_ERRFILE;
}


static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* Utf8 BOM mark */
  lf->n = 0;
  do {
    int c = getc(lf->f);
    if (c == EOF || c != *(unsigned char *)p++) return c;
    lf->buff[lf->n++] = (char)c;  /* to be read by the parser */
  } while (*p != '\0');
  lf->n = 0;  /* prefix matched; discard it */
  return getc(lf->f);  /* return next character */
}


/*
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment (LoadF *lf, int *cp) {
  int c = *cp = skipBOM(lf);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    while ((c = getc(lf->f)) != EOF && c != '\n') ;  /* skip first line */
    *cp = getc(lf->f);  /* skip end-of-line */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}


int luaL_loadfilex (LuaThread *L, const char *filename,
                                             const char *mode) {
  THREAD_CHECK(L);
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = L->stack_.getTopIndex() + 1;  /* index of filename on the stack */
  if (filename == NULL) {
    lua_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    lua_pushfstring(L, "@%s", filename);
    lf.f = fopen(filename, "r");
    if (lf.f == NULL) return errfile(L, "open", fnameindex);
  }
  if (skipcomment(&lf, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add line to correct line numbers */
  if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
    lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
    if (lf.f == NULL) return errfile(L, "reopen", fnameindex);
    skipcomment(&lf, &c);  /* re-read initial portion */
  }
  if (c != EOF) {
    // 'c' is the first character of the stream
    lf.buff[lf.n++] = (char)c;  
  }
  
  Zio2 z;
  z.init(L, getF, &lf);

  status = lua_load(L, &z, lua_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    L->stack_.setTopIndex(fnameindex);  /* ignore results from `lua_load' */
    return errfile(L, "read", fnameindex);
  }
  L->stack_.remove(fnameindex);
  return status;
}


int luaL_loadbufferx (LuaThread *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  THREAD_CHECK(L);
  Zio3 z(buff, size);
  return lua_load(L, &z, name, mode);
}


int luaL_loadstring (LuaThread *L, const char *s) {
  THREAD_CHECK(L);
  return luaL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */



int luaL_getmetafield (LuaThread *L, int obj, const char *event) {
  THREAD_CHECK(L);
  if (!lua_getmetatable(L, obj)) {
    /* no metatable? */
    return 0;
  }
  lua_pushstring(L, event);
  lua_rawget(L, -2);
  if (L->stack_.at(-1).isNil()) {
    L->stack_.pop(2);  /* remove metatable and metafield */
    return 0;
  }
  else {
    L->stack_.remove(-2);  /* remove only metatable */
    return 1;
  }
}


int luaL_callmeta (LuaThread *L, int obj, const char *event) {
  THREAD_CHECK(L);
  obj = lua_absindex(L, obj);
  if (!luaL_getmetafield(L, obj, event))  /* no metafield? */
    return 0;
  L->stack_.copy(obj);
  lua_call(L, 1, 1);
  return 1;
}


int luaL_len (LuaThread *L, int idx) {
  THREAD_CHECK(L);
  int l;
  int isnum;
  lua_len(L, idx);
  l = (int)lua_tointegerx(L, -1, &isnum);
  if (!isnum)
    luaL_error(L, "object length is not a number");
  L->stack_.pop();  /* remove object */
  return l;
}


const char *luaL_tolstring (LuaThread *L, int idx, size_t *len) {
  THREAD_CHECK(L);
  if (!luaL_callmeta(L, idx, "__tostring")) {  /* no metafield? */
    LuaValue* pv = index2addr(L, idx); 
    assert(pv);
    LuaValue v = *pv;

    if(v.isNumber() || v.isString()) {
      L->stack_.copy(idx);
    } else if(v.isBool()) {
      lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
    } else if(v.isNil()) {
      lua_pushliteral(L, "nil");
    } else {
      lua_pushfstring(L, "%s: %p", luaL_typename(L, idx),
                                          lua_topointer(L, idx));
    }
  }
  return lua_tolstring(L, -1, len);
}


/* }====================================================== */

void luaL_getregistrytable (LuaThread *L, const char *fname) {
  L->stack_.push( L->l_G->getRegistryTable(fname) );
}

std::string replace_all (const char* source,
                         const char* pattern,
                         const char* replace) {
  std::string result;
  size_t len = strlen(pattern);

  const char* match = strstr(source, pattern);
  while(match) {
    result += std::string(source, match - source);
    result += replace;
    source = match + len;
    match = strstr(source, pattern);
  }
  
  result += source;

  return result;
}


const char *luaL_gsub (LuaThread *L, const char *s, const char *p, const char *r) {
  THREAD_CHECK(L);

  std::string result = replace_all(s, p, r);

  LuaString* s2 = L->l_G->strings_->Create(result.c_str());
  L->stack_.push(s2);
  return s2->c_str();
}


static int panic (LuaThread *L) {
  THREAD_CHECK(L);
  luai_writestringerror("PANIC: unprotected error in call to Lua API (%s)\n",
                   lua_tostring(L, -1));
  return 0;  /* return to Lua to abort */
}


LuaThread *luaL_newstate (void) {
  LuaThread *L = lua_newstate();
  if (L) {
    GLOBAL_CHANGE(L);
    lua_atpanic(L, &panic);
  }
  return L;
}


void luaL_checkversion_ (LuaThread *L, double ver) {
  THREAD_CHECK(L);
  const double *v = lua_version(L);
  if (v != lua_version(NULL))
    luaL_error(L, "multiple Lua VMs detected");
  else if (*v != ver)
    luaL_error(L, "version mismatch: app. needs %f, Lua core provides %f",
                  ver, *v);
  /* check conversions number -> integer types */
  lua_pushnumber(L, -(double)0x1234);
  if (lua_tointeger(L, -1) != -0x1234 ||
      lua_tounsigned(L, -1) != (uint32_t)-0x1234)
    luaL_error(L, "bad conversion number->int;"
                  " must recompile Lua with proper settings");
  L->stack_.pop();
}

