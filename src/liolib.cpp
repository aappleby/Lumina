/*
** $Id: liolib.c,v 2.108 2011/11/25 12:50:03 roberto Exp $
** Standard I/O (and system) library
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaState.h"
#include "LuaUserdata.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"
#include "lstate.h" // for THREAD_CHECK

/*
** {======================================================
** lua_popen spawns a new process connected to the current
** one through the file streams.
** =======================================================
*/

#define lua_popen(L,c,m)		((void)L, _popen(c,m))
#define lua_pclose(L,file)	((void)L, _pclose(file))

/* }====================================================== */


#define IO_PREFIX	"_IO_"
#define IO_INPUT	(IO_PREFIX "input")
#define IO_OUTPUT	(IO_PREFIX "output")

/*
** {======================================================
** File handles for IO library
** =======================================================
*/

/*
** A file handle is a userdata with metatable 'LUA_FILEHANDLE' and
** initial structure 'luaL_Stream' (it may contain other fields
** after that initial structure).
*/

class LuaFile;

typedef void (*FileCloser)(LuaFile*);

class LuaFile : public LuaBlob
{
public:

  LuaFile() : LuaBlob(0) {
    f = NULL;
    closef = NULL;
    closef2 = NULL;
  }

  virtual ~LuaFile() {
  }

  FILE* getHandle() {
  }

  FILE *f;  /* stream (NULL for incompletely created streams) */
  LuaCallback closef;  /* to close stream (NULL for closed streams) */
  FileCloser closef2;
};

#define LUA_FILEHANDLE          "FILE*"

int typeerror (LuaThread *L, int narg, const char* type1);

LuaFile* getFile(LuaThread* L, int index) {
  THREAD_CHECK(L);
  void *p = luaL_testudata(L, index, LUA_FILEHANDLE);
  if (p == NULL) typeerror(L, index, LUA_FILEHANDLE);
  return dynamic_cast<LuaFile*>(L->stack_.at(index).getBlob());
}

#define isclosed(p)	((p)->closef == NULL)

static int io_type (LuaThread *L) {
  THREAD_CHECK(L);
  luaL_checkany(L, 1);
  void* p = luaL_testudata(L, 1, LUA_FILEHANDLE);

  if (p == NULL) {
    L->stack_.push(LuaValue::Nil());  /* not a file */
    return 1;
  }

  LuaFile* p2 = getFile(L,1);

  if (isclosed(p2)) {
    lua_pushliteral(L, "closed file");
    return 1;
  }
  else {
    lua_pushliteral(L, "file");
    return 1;
  }
}


static int f_tostring (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  if (isclosed(p2)) {
    lua_pushliteral(L, "file (closed)");
  }
  else {
    lua_pushfstring(L, "file (%p)", p2->f);
  }
  return 1;
}


static FILE *tofile (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  if (isclosed(p2))
    luaL_error(L, "attempt to use a closed file");
  assert(p2->f);
  return p2->f;
}


/*
** When creating file handles, always creates a `closed' file handle
** before opening the actual file; so, if there is a memory error, the
** file is not left opened.
*/
static LuaFile* newprefile (LuaThread *L) {
  THREAD_CHECK(L);

  LuaFile* u = new LuaFile();
  L->stack_.push(u);

  // TODO(aappleby): Files are closed in the finalizer, and just setting
  // the metatable_ field doesn't put the file on the finalization list -
  // we have to make sure they're on the 'finobj' list and marked as
  // separated.
  LuaTable* meta = L->l_G->getRegistryTable(LUA_FILEHANDLE);
  u->metatable_ = meta;

  u->unlinkGC(L->l_G->allgc);
  //u->next_ = L->l_G->finobj;
  //L->l_G->finobj = u;
  L->l_G->finobj.Push(u);
  u->setSeparated();
  
  return u;
}


static int aux_close (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  LuaCallback cf = p2->closef;
  p2->closef = NULL;  /* mark stream as closed */
  int result = (*cf)(L);  /* close it */
  return result;
}


static int io_close (LuaThread *L) {
  THREAD_CHECK(L);
  if (lua_isnone(L, 1))  /* no argument? */
    lua_getregistryfield(L, IO_OUTPUT);  /* use standard output */
  tofile(L);  /* make sure argument is an open stream */
  return aux_close(L);
}


static int f_gc (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  if (!isclosed(p2) && p2->f != NULL) {
    FileCloser cf = p2->closef2;
    p2->closef2 = NULL;  /* mark stream as closed */
    (*cf)(p2);  /* close it */
  }
  return 0;
}


/*
** function to close regular files
*/
static int io_fclose (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  int res = fclose(p2->f);
  //return luaL_fileresult(L, (res == 0), NULL);

  int en = errno;  /* calls to Lua API may change this value */
  if (res == 0) {
    lua_pushboolean(L, 1);
    return 1;
  }
  else {
    L->stack_.push(LuaValue::Nil());
    lua_pushfstring(L, "%s", strerror(en));
    lua_pushinteger(L, en);
    return 3;
  }
}

static void io_fclose2(LuaFile* f) {
  fclose(f->f);
}

static LuaFile* newfile (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = newprefile(L);
  p2->f = NULL;
  p2->closef = &io_fclose;
  p2->closef2 = &io_fclose2;
  return p2;
}


static void opencheck (LuaThread *L, const char *fname, const char *mode) {
  THREAD_CHECK(L);
  LuaFile* p2 = newfile(L);
  p2->f = fopen(fname, mode);
  if (p2->f == NULL)
    luaL_error(L, "cannot open file " LUA_QS " (%s)", fname, strerror(errno));
}


static int io_open (LuaThread *L) {
  THREAD_CHECK(L);
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  LuaFile* p2 = newfile(L);
  int i = 0;
  /* check whether 'mode' matches '[rwa]%+?b?' */
  if (!(mode[i] != '\0' && strchr("rwa", mode[i++]) != NULL &&
       (mode[i] != '+' || ++i) &&  /* skip if char is '+' */
       (mode[i] != 'b' || ++i) &&  /* skip if char is 'b' */
       (mode[i] == '\0')))
    return luaL_error(L, "invalid mode " LUA_QS
                         " (should match " LUA_QL("[rwa]%%+?b?") ")", mode);
  p2->f = fopen(filename, mode);
  return (p2->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}


/*
** function to close 'popen' files
*/
static int io_pclose (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  return luaL_execresult(L, lua_pclose(L, p2->f));
}

static void io_pclose2(LuaFile* f) {
  _pclose(f->f);
}


static int io_popen (LuaThread *L) {
  THREAD_CHECK(L);
  const char *filename = luaL_checkstring(L, 1);
  const char *mode = luaL_optstring(L, 2, "r");
  LuaFile* p2 = newprefile(L);
  p2->f = lua_popen(L, filename, mode);
  p2->closef = &io_pclose;
  p2->closef2 = &io_pclose2;
  return (p2->f == NULL) ? luaL_fileresult(L, 0, filename) : 1;
}


static int io_tmpfile (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = newfile(L);
  p2->f = tmpfile();
  return (p2->f == NULL) ? luaL_fileresult(L, 0, NULL) : 1;
}


static FILE *getiofile (LuaThread *L, const char *findex) {
  THREAD_CHECK(L);
  lua_getregistryfield(L, findex);
  LuaFile* p2 = getFile(L,-1);
  if (isclosed(p2))
    luaL_error(L, "standard %s file is closed", findex + strlen(IO_PREFIX));
  return p2->f;
}


static int g_iofile (LuaThread *L, const char *f, const char *mode) {
  THREAD_CHECK(L);
  if (!lua_isnoneornil(L, 1)) {
    const char *filename = lua_tostring(L, 1);
    if (filename)
      opencheck(L, filename, mode);
    else {
      tofile(L);  /* check that it's a valid file handle */
      L->stack_.copy(1);
    }
    lua_setregistryfield(L, f);
  }
  /* return current value */
  lua_getregistryfield(L, f);
  return 1;
}


static int io_input (LuaThread *L) {
  THREAD_CHECK(L);
  return g_iofile(L, IO_INPUT, "r");
}


static int io_output (LuaThread *L) {
  THREAD_CHECK(L);
  return g_iofile(L, IO_OUTPUT, "w");
}


static int io_readline (LuaThread *L);


static void aux_lines (LuaThread *L, int toclose) {
  THREAD_CHECK(L);
  int i;
  int n = L->stack_.getTopIndex() - 1;  /* number of arguments to read */
  /* ensure that arguments will fit here and into 'io_readline' stack */
  luaL_argcheck(L, n <= LUA_MINSTACK - 3, LUA_MINSTACK - 3, "too many options");
  L->stack_.copy(1);  /* file handle */
  lua_pushinteger(L, n);  /* number of arguments to read */
  lua_pushboolean(L, toclose);  /* close/not close file when finished */
  for (i = 1; i <= n; i++) L->stack_.copy(i + 1);  /* copy arguments */
  lua_pushcclosure(L, io_readline, 3 + n);
}


static int f_lines (LuaThread *L) {
  THREAD_CHECK(L);
  tofile(L);  /* check that it's a valid file handle */
  aux_lines(L, 0);
  return 1;
}


static int io_lines (LuaThread *L) {
  THREAD_CHECK(L);
  int toclose;
  if (lua_isnone(L, 1)) L->stack_.push(LuaValue::Nil());  /* at least one argument */

  if (lua_isnil(L, 1)) {  /* no file name? */
    lua_getregistryfield(L, IO_INPUT);  /* get default input */
    lua_replace(L, 1);  /* put it at index 1 */
    tofile(L);  /* check that it's a valid file handle */
    toclose = 0;  /* do not close it after iteration */
  }
  else {  /* open a new file */
    const char *filename = luaL_checkstring(L, 1);
    opencheck(L, filename, "r");
    lua_replace(L, 1);  /* put file at index 1 */
    toclose = 1;  /* close it after iteration */
  }
  aux_lines(L, toclose);
  return 1;
}


/*
** {======================================================
** READ
** =======================================================
*/


static int read_number (LuaThread *L, FILE *f) {
  THREAD_CHECK(L);
  double d;
  if (fscanf(f, LUA_NUMBER_SCAN, &d) == 1) {
    lua_pushnumber(L, d);
    return 1;
  }
  else {
   L->stack_.push(LuaValue::Nil());  /* "result" to be removed */
   return 0;  /* read fails */
  }
}


static int test_eof (LuaThread *L, FILE *f) {
  THREAD_CHECK(L);
  int c = getc(f);
  ungetc(c, f);
  lua_pushlstring(L, NULL, 0);
  return (c != EOF);
}


static int read_line (LuaThread *L, FILE *f, int chop) {
  THREAD_CHECK(L);
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (;;) {
    size_t l;
    char *p = luaL_prepbuffer(&b);
    if (fgets(p, LUAL_BUFFERSIZE, f) == NULL) {  /* eof? */
      luaL_pushresult(&b);  /* close buffer */
      return (lua_rawlen(L, -1) > 0);  /* check whether read something */
    }
    l = strlen(p);
    if (l == 0 || p[l-1] != '\n')
      luaL_addsize(&b, l);
    else {
      luaL_addsize(&b, l - chop);  /* chop 'eol' if needed */
      luaL_pushresult(&b);  /* close buffer */
      return 1;  /* read at least an `eol' */
    }
  }
}


#define MAX_SIZE_T	(~(size_t)0)

static void read_all (LuaThread *L, FILE *f) {
  THREAD_CHECK(L);
  size_t rlen = LUAL_BUFFERSIZE;  /* how much to read in each cycle */
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (;;) {
    char *p = luaL_prepbuffsize(&b, rlen);
    size_t nr = fread(p, sizeof(char), rlen, f);
    luaL_addsize(&b, nr);
    if (nr < rlen) break;  /* eof? */
    else if (rlen <= (MAX_SIZE_T / 4))  /* avoid buffers too large */
      rlen *= 2;  /* double buffer size at each iteration */
  }
  luaL_pushresult(&b);  /* close buffer */
}


static int read_chars (LuaThread *L, FILE *f, size_t n) {
  THREAD_CHECK(L);
  size_t nr;  /* number of chars actually read */
  char *p;
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  p = luaL_prepbuffsize(&b, n);  /* prepare buffer to read whole block */
  nr = fread(p, sizeof(char), n, f);  /* try to read 'n' chars */
  luaL_addsize(&b, nr);
  luaL_pushresult(&b);  /* close buffer */
  return (nr > 0);  /* true iff read something */
}


static int g_read (LuaThread *L, FILE *f, int first) {
  THREAD_CHECK(L);
  int nargs = L->stack_.getTopIndex() - 1;
  int success;
  int n;
  clearerr(f);
  if (nargs == 0) {  /* no arguments? */
    success = read_line(L, f, 1);
    n = first+1;  /* to return 1 result */
  }
  else {  /* ensure stack space for all results and for auxlib's buffer */
    luaL_checkstack(L, nargs+LUA_MINSTACK, "too many arguments");
    success = 1;
    for (n = first; nargs-- && success; n++) {
      if (lua_type(L, n) == LUA_TNUMBER) {
        size_t l = (size_t)lua_tointeger(L, n);
        success = (l == 0) ? test_eof(L, f) : read_chars(L, f, l);
      }
      else {
        const char *p = lua_tostring(L, n);
        luaL_argcheck(L, p && p[0] == '*', n, "invalid option");
        switch (p[1]) {
          case 'n':  /* number */
            success = read_number(L, f);
            break;
          case 'l':  /* line */
            success = read_line(L, f, 1);
            break;
          case 'L':  /* line with end-of-line */
            success = read_line(L, f, 0);
            break;
          case 'a':  /* file */
            read_all(L, f);  /* read entire file */
            success = 1; /* always success */
            break;
          default:
            return luaL_argerror(L, n, "invalid format");
        }
      }
    }
  }
  if (ferror(f))
    return luaL_fileresult(L, 0, NULL);
  if (!success) {
    L->stack_.pop();  /* remove last result */
    L->stack_.push(LuaValue::Nil());  /* push nil instead */
  }
  return n - first;
}


static int io_read (LuaThread *L) {
  THREAD_CHECK(L);
  return g_read(L, getiofile(L, IO_INPUT), 1);
}


static int f_read (LuaThread *L) {
  THREAD_CHECK(L);
  return g_read(L, tofile(L), 2);
}


static int io_readline (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,lua_upvalueindex(1));
  int i;
  int n = (int)lua_tointeger(L, lua_upvalueindex(2));
  if (isclosed(p2))  /* file is already closed? */
    return luaL_error(L, "file is already closed");
  L->stack_.setTopIndex(1);
  for (i = 1; i <= n; i++)  /* push arguments to 'g_read' */
    L->stack_.copy(lua_upvalueindex(3 + i));
  n = g_read(L, p2->f, 2);  /* 'n' is number of results */
  assert(n > 0);  /* should return at least a nil */
  if (!lua_isnil(L, -n))  /* read at least one value? */
    return n;  /* return them */
  else {  /* first result is nil: EOF or error */
    if (n > 1) {  /* is there error information? */
      /* 2nd result is error message */
      return luaL_error(L, "%s", lua_tostring(L, -n + 1));
    }
    if (lua_toboolean(L, lua_upvalueindex(3))) {  /* generator created file? */
      L->stack_.setTopIndex(0);
      L->stack_.copy(lua_upvalueindex(1));
      aux_close(L);  /* close it */
    }
    return 0;
  }
}

/* }====================================================== */


static int g_write (LuaThread *L, FILE *f, int arg) {
  THREAD_CHECK(L);
  int nargs = L->stack_.getTopIndex() - arg;
  int status = 1;
  for (; nargs--; arg++) {
    if (lua_type(L, arg) == LUA_TNUMBER) {
      /* optimization: could be done exactly as for strings */
      status = status &&
          fprintf(f, LUA_NUMBER_FMT, lua_tonumber(L, arg)) > 0;
    }
    else {
      size_t l;
      const char *s = luaL_checklstring(L, arg, &l);
      status = status && (fwrite(s, sizeof(char), l, f) == l);
    }
  }
  if (status) return 1;  /* file handle already on stack top */
  else return luaL_fileresult(L, status, NULL);
}


static int io_write (LuaThread *L) {
  THREAD_CHECK(L);
  return g_write(L, getiofile(L, IO_OUTPUT), 1);
}


static int f_write (LuaThread *L) {
  THREAD_CHECK(L);
  FILE *f = tofile(L);
  L->stack_.copy(1);  /* push file at the stack top (to be returned) */
  return g_write(L, f, 2);
}


static int f_seek (LuaThread *L) {
  THREAD_CHECK(L);
  static const int mode[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  static const char *const modenames[] = {"set", "cur", "end", NULL};
  FILE *f = tofile(L);
  int op = luaL_checkoption(L, 2, "cur", modenames);
  double p3 = luaL_optnumber(L, 3, 0);
  int offset = (int)p3;
  luaL_argcheck(L, (double)offset == p3, 3,
                  "not an integer in proper range");
  op = fseek(f, offset, mode[op]);
  if (op)
    return luaL_fileresult(L, 0, NULL);  /* error */
  else {
    lua_pushnumber(L, (double)ftell(f));
    return 1;
  }
}


static int f_setvbuf (LuaThread *L) {
  THREAD_CHECK(L);
  static const int mode[] = {_IONBF, _IOFBF, _IOLBF};
  static const char *const modenames[] = {"no", "full", "line", NULL};
  FILE *f = tofile(L);
  int op = luaL_checkoption(L, 2, NULL, modenames);
  ptrdiff_t sz = luaL_optinteger(L, 3, LUAL_BUFFERSIZE);
  int res = setvbuf(f, NULL, mode[op], sz);
  return luaL_fileresult(L, res == 0, NULL);
}



static int io_flush (LuaThread *L) {
  THREAD_CHECK(L);
  return luaL_fileresult(L, fflush(getiofile(L, IO_OUTPUT)) == 0, NULL);
}


static int f_flush (LuaThread *L) {
  THREAD_CHECK(L);
  return luaL_fileresult(L, fflush(tofile(L)) == 0, NULL);
}


/*
** functions for 'io' library
*/
static const luaL_Reg iolib[] = {
  {"close", io_close},
  {"flush", io_flush},
  {"input", io_input},
  {"lines", io_lines},
  {"open", io_open},
  {"output", io_output},
  {"popen", io_popen},
  {"read", io_read},
  {"tmpfile", io_tmpfile},
  {"type", io_type},
  {"write", io_write},
  {NULL, NULL}
};


/*
** methods for file handles
*/
static const luaL_Reg flib[] = {
  {"close", io_close},
  {"flush", f_flush},
  {"lines", f_lines},
  {"read", f_read},
  {"seek", f_seek},
  {"setvbuf", f_setvbuf},
  {"write", f_write},
  {"__gc", f_gc},
  {"__tostring", f_tostring},
  {NULL, NULL}
};


/*
** function to (not) close the standard files stdin, stdout, and stderr
*/
static int io_noclose (LuaThread *L) {
  THREAD_CHECK(L);
  LuaFile* p2 = getFile(L,1);
  p2->closef = &io_noclose;  /* keep file opened */
  L->stack_.push(LuaValue::Nil());
  lua_pushliteral(L, "cannot close standard file");
  return 2;
}

static void io_noclose2(LuaFile*) {
}


static void createstdfile (LuaThread *L, FILE *f, const char *k,
                           const char *fname) {
  THREAD_CHECK(L);
  LuaFile* p2 = newprefile(L);
  p2->f = f;
  p2->closef = &io_noclose;
  p2->closef2 = &io_noclose2;
  if (k != NULL) {
    L->stack_.copy(-1);
    lua_setregistryfield(L, k);  /* add file to registry */
  }
  lua_setfield(L, -2, fname);  /* add file to module */
}


int luaopen_io (LuaThread *L) {
  THREAD_CHECK(L);

  LuaTable* lib = new LuaTable();
  for(const luaL_Reg* cursor = iolib; cursor->name; cursor++) {
    lib->set( cursor->name, cursor->func );
  }

  L->stack_.push(lib);

  /* create metatable for file handles */
  THREAD_CHECK(L);

  LuaTable* meta = L->l_G->getRegistryTable(LUA_FILEHANDLE);
  meta->set("__index", meta);

  for(const luaL_Reg* cursor = flib; cursor->name; cursor++) {
    meta->set( cursor->name, cursor->func );
  }

  /* create (and set) default files */
  createstdfile(L, stdin, IO_INPUT, "stdin");
  createstdfile(L, stdout, IO_OUTPUT, "stdout");
  createstdfile(L, stderr, NULL, "stderr");
  return 1;
}

