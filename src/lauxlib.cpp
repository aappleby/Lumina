/*
** $Id: lauxlib.c,v 1.240 2011/12/06 16:33:55 roberto Exp $
** Auxiliary functions for building Lua libraries
** See Copyright Notice in lua.h
*/

#include "LuaState.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* This file uses only the official API of Lua.
** Any function declared here could be written as an application function.
*/

#define lauxlib_c
#define LUA_LIB

#include "lua.h"

#include "lauxlib.h"
#include "lmem.h"

#include "lstate.h" // for THREAD_CHECK

TValue *index2addr (lua_State *L, int idx);

const char* luaL_typename(lua_State* L, int index) {
  THREAD_CHECK(L);
  TValue v = index2addr3(L, index);
  return v.typeName();
}

/*
** {======================================================
** Traceback
** =======================================================
*/


#define LEVELS1	12	/* size of the first part of the stack */
#define LEVELS2	10	/* size of the second part of the stack */



/*
** search for 'objidx' in table at index -1.
** return 1 + string at top if find a good name.
*/
static int findfield (lua_State *L, int objidx, int level) {
  THREAD_CHECK(L);
  if (level == 0 || !lua_istable(L, -1))
    return 0;  /* not found */
  L->stack_.push(TValue::Nil());  /* start 'next' loop */
  while (lua_next(L, -2)) {  /* for each pair in table */
    if (lua_type(L, -2) == LUA_TSTRING) {  /* ignore non-string keys */
      if (lua_rawequal(L, objidx, -1)) {  /* found object? */
        L->stack_.pop();  /* remove value (but keep name) */
        return 1;
      }
      else if (findfield(L, objidx, level - 1)) {  /* try recursively */
        L->stack_.remove(-2);  /* remove table (but keep name) */
        luaC_checkGC();
        lua_pushliteral(L, ".");
        lua_insert(L, -2);  /* place '.' between the two names */
        lua_concat(L, 3);
        return 1;
      }
    }
    L->stack_.pop();  /* remove value */
  }
  return 0;  /* not found */
}


static int pushglobalfuncname (lua_State *L, lua_Debug *ar) {
  THREAD_CHECK(L);
  int top = L->stack_.getTopIndex();
  lua_getinfo(L, "f", ar);  /* push function */
  lua_pushglobaltable(L);
  if (findfield(L, top + 1, 2)) {
    lua_copy(L, -1, top + 1);  /* move name to proper place */
    L->stack_.pop(2);  /* remove pushed values */
    return 1;
  }
  else {
    L->stack_.setTopIndex(top);  /* remove function and global table */
    return 0;
  }
}


static void pushfuncname (lua_State *L, lua_Debug *ar) {
  THREAD_CHECK(L);
  if (*ar->namewhat != '\0')  /* is there a name? */
    lua_pushfstring(L, "function " LUA_QS, ar->name);
  else if (*ar->what == 'm')  /* main? */
      lua_pushfstring(L, "main chunk");
  else if (*ar->what == 'C') {
    if (pushglobalfuncname(L, ar)) {
      lua_pushfstring(L, "function " LUA_QS, lua_tostring(L, -1));
      L->stack_.remove(-2);  /* remove name */
    }
    else {
      luaC_checkGC();
      lua_pushliteral(L, "?");
    }
  }
  else {
    lua_pushfstring(L, "function <%s:%d>", ar->short_src, ar->linedefined);
  }
}


static int countlevels (lua_State *L) {
  THREAD_CHECK(L);
  lua_Debug ar;
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


void luaL_traceback (lua_State *L, lua_State *L1,
                                const char *msg, int level) {
  THREAD_CHECK(L);
  lua_Debug ar;
  int top = L->stack_.getTopIndex();
  int numlevels;
  {
    THREAD_CHANGE(L1);
    numlevels = countlevels(L1);
  }
  int mark = (numlevels > LEVELS1 + LEVELS2) ? LEVELS1 : 0;
  if (msg) lua_pushfstring(L, "%s\n", msg);
  luaC_checkGC();
  lua_pushliteral(L, "stack traceback:");
  {
    THREAD_CHANGE(L1);
    while (lua_getstack(L1, level++, &ar)) {
      if (level == mark) {  /* too many levels? */
        {
          THREAD_CHANGE(L);
          luaC_checkGC();
          lua_pushliteral(L, "\n\t...");  /* add a '...' */
        }
        level = numlevels - LEVELS2;  /* and skip to last ones */
      }
      else {
        lua_getinfo(L1, "Slnt", &ar);
        {
          THREAD_CHANGE(L);
          lua_pushfstring(L, "\n\t%s:", ar.short_src);
          if (ar.currentline > 0) {
            lua_pushfstring(L, "%d:", ar.currentline);
          }
          luaC_checkGC();
          lua_pushliteral(L, " in ");
          pushfuncname(L, &ar);
          if (ar.istailcall) {
            luaC_checkGC();
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

int luaL_argerror (lua_State *L, int narg, const char *extramsg) {
  THREAD_CHECK(L);
  lua_Debug ar;
  if (!lua_getstack(L, 0, &ar))  /* no stack frame? */
    return luaL_error(L, "bad argument #%d (%s)", narg, extramsg);
  lua_getinfo(L, "n", &ar);
  if (strcmp(ar.namewhat, "method") == 0) {
    narg--;  /* do not count `self' */
    if (narg == 0)  /* error is in the self argument itself? */
      return luaL_error(L, "calling " LUA_QS " on bad self", ar.name);
  }
  if (ar.name == NULL)
    ar.name = (pushglobalfuncname(L, &ar)) ? lua_tostring(L, -1) : "?";
  return luaL_error(L, "bad argument #%d to " LUA_QS " (%s)",
                        narg, ar.name, extramsg);
}


static int typeerror (lua_State *L, int narg, const char* type1) {
  THREAD_CHECK(L);
  const char* type2 = luaL_typename(L, narg);
  const char *msg = lua_pushfstring(L, "%s expected, got %s", type1, type2);
  return luaL_argerror(L, narg, msg);
}


static void tag_error (lua_State *L, int narg, int tag) {
  THREAD_CHECK(L);
  typeerror(L, narg, lua_typename(L, tag));
}


void luaL_where (lua_State *L, int level) {
  THREAD_CHECK(L);
  lua_Debug ar;
  if (lua_getstack(L, level, &ar)) {  /* check function at level */
    lua_getinfo(L, "Sl", &ar);  /* get info about it */
    if (ar.currentline > 0) {  /* is there info? */
      lua_pushfstring(L, "%s:%d: ", ar.short_src, ar.currentline);
      return;
    }
  }
  luaC_checkGC();
  lua_pushliteral(L, "");  /* else, no information available... */
}


int luaL_error (lua_State *L, const char *fmt, ...) {
  THREAD_CHECK(L);
  va_list argp;
  va_start(argp, fmt);
  luaL_where(L, 1);
  lua_pushvfstring(L, fmt, argp);
  va_end(argp);
  lua_concat(L, 2);
  return lua_error(L);
}


int luaL_fileresult (lua_State *L, int stat, const char *fname) {
  THREAD_CHECK(L);
  int en = errno;  /* calls to Lua API may change this value */
  if (stat) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    L->stack_.push(TValue::Nil());
    if (fname)
      lua_pushfstring(L, "%s: %s", fname, strerror(en));
    else
      lua_pushfstring(L, "%s", strerror(en));
    lua_pushinteger(L, en);
    return 3;
  }
}


int luaL_execresult (lua_State *L, int stat) {
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
      L->stack_.push(TValue::Nil());
    }
    luaC_checkGC();
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

int luaL_newmetatable (lua_State *L, const char *tname) {
  THREAD_CHECK(L);
  luaL_getmetatable(L, tname);  /* try to get metatable */
  if (!lua_isnil(L, -1))  /* name already in use? */
    return 0;  /* leave previous value on top, but return 0 */
  L->stack_.pop();
  luaC_checkGC();
  lua_createtable(L, 0, 0);
  L->stack_.copy(-1);
  lua_setfield(L, LUA_REGISTRYINDEX, tname);  /* registry.name = metatable */
  return 1;
}


void luaL_setmetatable (lua_State *L, const char *tname) {
  THREAD_CHECK(L);
  luaL_getmetatable(L, tname);
  lua_setmetatable(L, -2);
}


void *luaL_testudata (lua_State *L, int ud, const char *tname) {
  THREAD_CHECK(L);
  void *p = lua_touserdata(L, ud);
  if (p != NULL) {  /* value is a userdata? */
    if (lua_getmetatable(L, ud)) {  /* does it have a metatable? */
      luaL_getmetatable(L, tname);  /* get correct metatable */
      if (!lua_rawequal(L, -1, -2))  /* not the same? */
        p = NULL;  /* value is a userdata with wrong metatable */
      L->stack_.pop(2);  /* remove both metatables */
      return p;
    }
  }
  return NULL;  /* value is not a userdata with a metatable */
}


void *luaL_checkudata (lua_State *L, int ud, const char *tname) {
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

int luaL_checkoption (lua_State *L,
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
  return luaL_argerror(L, narg,
                       lua_pushfstring(L, "invalid option " LUA_QS, name));
}


void luaL_checkstack (lua_State *L, int space, const char *msg) {
  THREAD_CHECK(L);
  /* keep some extra space to run error routines, if needed */
  const int extra = LUA_MINSTACK;
  if (!lua_checkstack(L, space + extra)) {
    if (msg)
      luaL_error(L, "stack overflow (%s)", msg);
    else
      luaL_error(L, "stack overflow");
  }
}

void luaL_checkIsFunction(lua_State *L, int narg) {
  THREAD_CHECK(L);
  TValue* pv = index2addr(L, narg);
  assert(pv);
  TValue v = *pv;
  if(v.isFunction()) return;
  const char* actualType = v.typeName();
  const char *msg = lua_pushfstring(L, "Expected a function, got a %s", actualType);
  luaL_argerror(L, narg, msg);
}

void luaL_checkIsTable(lua_State* L, int narg) {
  THREAD_CHECK(L);
  TValue v = *index2addr(L, narg);
  if(v.isTable()) return;
  const char* actualType = v.typeName();
  const char *msg = lua_pushfstring(L, "Expected a table, got a %s", actualType);
  luaL_argerror(L, narg, msg);
}

void luaL_checktype (lua_State *L, int narg, int t) {
  THREAD_CHECK(L);
  if (lua_type(L, narg) != t)
    tag_error(L, narg, t);
}


void luaL_checkany (lua_State *L, int narg) {
  THREAD_CHECK(L);
  TValue* v = index2addr2(L, narg);
  if(v == NULL) {
    luaL_argerror(L, narg, "value expected");
  }
}


const char *luaL_checklstring (lua_State *L, int narg, size_t *len) {
  THREAD_CHECK(L);
  const char *s = lua_tolstring(L, narg, len);
  if (!s) tag_error(L, narg, LUA_TSTRING);
  return s;
}


const char *luaL_optlstring (lua_State *L,
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


lua_Number luaL_checknumber (lua_State *L, int narg) {
  THREAD_CHECK(L);
  TValue v1 = index2addr3(L, narg);
  TValue v2 = v1.convertToNumber();
  if(v2.isNone()) {
    tag_error(L, narg, LUA_TNUMBER);
  }
  return v2.getNumber();
}


lua_Number luaL_optnumber (lua_State *L, int narg, lua_Number def) {
  THREAD_CHECK(L);
  return luaL_opt(L, luaL_checknumber, narg, def);
}


lua_Integer luaL_checkinteger (lua_State *L, int narg) {
  THREAD_CHECK(L);
  int isnum;
  lua_Integer d = lua_tointegerx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


lua_Unsigned luaL_checkunsigned (lua_State *L, int narg) {
  THREAD_CHECK(L);
  int isnum;
  lua_Unsigned d = lua_tounsignedx(L, narg, &isnum);
  if (!isnum)
    tag_error(L, narg, LUA_TNUMBER);
  return d;
}


lua_Integer luaL_optinteger (lua_State *L, int narg, lua_Integer def) {
  THREAD_CHECK(L);
  return luaL_opt(L, luaL_checkinteger, narg, def);
}


lua_Unsigned luaL_optunsigned (lua_State *L, int narg, lua_Unsigned def) {
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
  lua_State *L = B->L;
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
  lua_State *L = B->L;
  luaC_checkGC();
  lua_pushlstring(L, B->b, B->n);
  if (buffonstack(B))
    L->stack_.remove(-2);  /* remove old buffer */
}


void luaL_pushresultsize (luaL_Buffer *B, size_t sz) {
  luaL_addsize(B, sz);
  luaL_pushresult(B);
}


void luaL_addvalue (luaL_Buffer *B) {
  lua_State *L = B->L;
  size_t l;
  const char *s = lua_tolstring(L, -1, &l);
  if (buffonstack(B))
    lua_insert(L, -2);  /* put value below buffer */
  luaL_addlstring(B, s, l);
  L->stack_.remove((buffonstack(B)) ? -2 : -1);  /* remove value */
}


void luaL_buffinit (lua_State *L, luaL_Buffer *B) {
  THREAD_CHECK(L);
  B->L = L;
  B->b = B->initb;
  B->n = 0;
  B->size = LUAL_BUFFERSIZE;
}


char *luaL_buffinitsize (lua_State *L, luaL_Buffer *B, size_t sz) {
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


int luaL_ref (lua_State *L, int t) {
  THREAD_CHECK(L);
  int ref;
  t = lua_absindex(L, t);
  if (lua_isnil(L, -1)) {
    L->stack_.pop();  /* remove from stack */
    return LUA_REFNIL;  /* `nil' has a unique fixed reference */
  }
  lua_rawgeti(L, t, freelist);  /* get first free element */
  ref = (int)lua_tointeger(L, -1);  /* ref = t[freelist] */
  L->stack_.pop();  /* remove it from stack */
  if (ref != 0) {  /* any free element? */
    lua_rawgeti(L, t, ref);  /* remove it from list */
    lua_rawseti(L, t, freelist);  /* (t[freelist] = t[ref]) */
  }
  else  /* no free elements */
    ref = (int)lua_rawlen(L, t) + 1;  /* get a new reference */
  lua_rawseti(L, t, ref);
  return ref;
}


void luaL_unref (lua_State *L, int t, int ref) {
  THREAD_CHECK(L);
  if (ref >= 0) {
    t = lua_absindex(L, t);
    lua_rawgeti(L, t, freelist);
    lua_rawseti(L, t, ref);  /* t[ref] = t[freelist] */
    lua_pushinteger(L, ref);
    lua_rawseti(L, t, freelist);  /* t[freelist] = ref */
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


static const char *getF (lua_State *L, void *ud, size_t *size) {
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


static int errfile (lua_State *L, const char *what, int fnameindex) {
  THREAD_CHECK(L);
  const char *serr = strerror(errno);
  const char *filename = lua_tostring(L, fnameindex) + 1;
  lua_pushfstring(L, "cannot %s %s: %s", what, filename, serr);
  L->stack_.remove(fnameindex);
  return LUA_ERRFILE;
}


static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* Utf8 BOM mark */
  int c;
  lf->n = 0;
  do {
    c = getc(lf->f);
    if (c == EOF || c != *(unsigned char *)p++) return c;
    lf->buff[lf->n++] = c;  /* to be read by the parser */
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


int luaL_loadfilex (lua_State *L, const char *filename,
                                             const char *mode) {
  THREAD_CHECK(L);
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = L->stack_.getTopIndex() + 1;  /* index of filename on the stack */
  if (filename == NULL) {
    luaC_checkGC();
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
  if (c != EOF)
    lf.buff[lf.n++] = c;  /* 'c' is the first character of the stream */
  status = lua_load(L, getF, &lf, lua_tostring(L, -1), mode);
  readstatus = ferror(lf.f);
  if (filename) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    L->stack_.setTopIndex(fnameindex);  /* ignore results from `lua_load' */
    return errfile(L, "read", fnameindex);
  }
  L->stack_.remove(fnameindex);
  return status;
}


typedef struct LoadS {
  const char *s;
  size_t size;
} LoadS;


static const char *getS (lua_State *L, void *ud, size_t *size) {
  THREAD_CHECK(L);
  LoadS *ls = (LoadS *)ud;
  (void)L;  /* not used */
  if (ls->size == 0) return NULL;
  *size = ls->size;
  ls->size = 0;
  return ls->s;
}


int luaL_loadbufferx (lua_State *L, const char *buff, size_t size,
                                 const char *name, const char *mode) {
  THREAD_CHECK(L);
  LoadS ls;
  ls.s = buff;
  ls.size = size;
  return lua_load(L, getS, &ls, name, mode);
}


int luaL_loadstring (lua_State *L, const char *s) {
  THREAD_CHECK(L);
  return luaL_loadbuffer(L, s, strlen(s), s);
}

/* }====================================================== */



int luaL_getmetafield (lua_State *L, int obj, const char *event) {
  THREAD_CHECK(L);
  if (!lua_getmetatable(L, obj)) {
    /* no metatable? */
    return 0;
  }
  luaC_checkGC();
  lua_pushstring(L, event);
  lua_rawget(L, -2);
  if (lua_isnil(L, -1)) {
    L->stack_.pop(2);  /* remove metatable and metafield */
    return 0;
  }
  else {
    L->stack_.remove(-2);  /* remove only metatable */
    return 1;
  }
}


int luaL_callmeta (lua_State *L, int obj, const char *event) {
  THREAD_CHECK(L);
  obj = lua_absindex(L, obj);
  if (!luaL_getmetafield(L, obj, event))  /* no metafield? */
    return 0;
  L->stack_.copy(obj);
  lua_call(L, 1, 1);
  return 1;
}


int luaL_len (lua_State *L, int idx) {
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


const char *luaL_tolstring (lua_State *L, int idx, size_t *len) {
  THREAD_CHECK(L);
  if (!luaL_callmeta(L, idx, "__tostring")) {  /* no metafield? */
    TValue* pv = index2addr(L, idx); 
    assert(pv);
    TValue v = *pv;

    if(v.isNumber() || v.isString()) {
      L->stack_.copy(idx);
    } else if(v.isBool()) {
      luaC_checkGC();
      lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
    } else if(v.isNil()) {
      luaC_checkGC();
      lua_pushliteral(L, "nil");
    } else {
      lua_pushfstring(L, "%s: %p", luaL_typename(L, idx),
                                          lua_topointer(L, idx));
    }
  }
  return lua_tolstring(L, -1, len);
}


/* }====================================================== */

/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
void luaL_setfuncs (lua_State *L, const luaL_Reg *l, int nup) {
  THREAD_CHECK(L);
  luaL_checkstack(L, nup, "too many upvalues");
  for (; l->name != NULL; l++) {  /* fill the table with given functions */
    int i;
    for (i = 0; i < nup; i++) {
      /* copy upvalues to the top */
      L->stack_.copy(-nup);
    }
    lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
    lua_setfield(L, -(nup + 2), l->name);
  }
  L->stack_.pop(nup);  /* remove upvalues */
}


/*
** ensure that stack[idx][fname] has a table and push that table
** into the stack
*/
int luaL_getsubtable (lua_State *L, int idx, const char *fname) {
  THREAD_CHECK(L);
  lua_getfield(L, idx, fname);
  if (lua_istable(L, -1)) return 1;  /* table already there */
  else {
    idx = lua_absindex(L, idx);
    L->stack_.pop();  /* remove previous result */
    lua_createtable(L, 0, 0);
    L->stack_.copy(-1);  /* copy to be left at top */
    lua_setfield(L, idx, fname);  /* assign new table to field */
    return 0;  /* false, because did not find table there */
  }
}


/*
** stripped-down 'require'. Calls 'openf' to open a module,
** registers the result in 'package.loaded' table and, if 'glb'
** is true, also registers the result in the global table.
** Leaves resulting module on the top.
*/
void luaL_requiref (lua_State *L, const char *modname,
                               lua_CFunction openf, int glb) {
  THREAD_CHECK(L);
  lua_pushcfunction(L, openf);
  luaC_checkGC();
  lua_pushstring(L, modname);  /* argument to open function */
  lua_call(L, 1, 1);  /* open module */
  luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
  L->stack_.copy(-2);  /* make copy of module (call result) */
  lua_setfield(L, -2, modname);  /* _LOADED[modname] = module */
  L->stack_.pop();  /* remove _LOADED table */
  if (glb) {
    lua_pushglobaltable(L);
    L->stack_.copy(-2);  /* copy of 'mod' */
    lua_setfield(L, -2, modname);  /* _G[modname] = module */
    L->stack_.pop();  /* remove _G table */
  }
}


const char *luaL_gsub (lua_State *L, const char *s, const char *p,
                                                               const char *r) {
  THREAD_CHECK(L);
  const char *wild;
  size_t l = strlen(p);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  while ((wild = strstr(s, p)) != NULL) {
    luaL_addlstring(&b, s, wild - s);  /* push prefix */
    luaL_addstring(&b, r);  /* push replacement in place of pattern */
    s = wild + l;  /* continue after `p' */
  }
  luaL_addstring(&b, s);  /* push last suffix */
  luaL_pushresult(&b);
  return lua_tostring(L, -1);
}


static int panic (lua_State *L) {
  THREAD_CHECK(L);
  luai_writestringerror("PANIC: unprotected error in call to Lua API (%s)\n",
                   lua_tostring(L, -1));
  return 0;  /* return to Lua to abort */
}


lua_State *luaL_newstate (void) {
  lua_State *L = lua_newstate();
  if (L) {
    GLOBAL_CHANGE(L);
    lua_atpanic(L, &panic);
  }
  return L;
}


void luaL_checkversion_ (lua_State *L, lua_Number ver) {
  THREAD_CHECK(L);
  const lua_Number *v = lua_version(L);
  if (v != lua_version(NULL))
    luaL_error(L, "multiple Lua VMs detected");
  else if (*v != ver)
    luaL_error(L, "version mismatch: app. needs %f, Lua core provides %f",
                  ver, *v);
  /* check conversions number -> integer types */
  lua_pushnumber(L, -(lua_Number)0x1234);
  if (lua_tointeger(L, -1) != -0x1234 ||
      lua_tounsigned(L, -1) != (lua_Unsigned)-0x1234)
    luaL_error(L, "bad conversion number->int;"
                  " must recompile Lua with proper settings");
  L->stack_.pop();
}

