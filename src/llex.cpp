/*
** $Id: llex.c,v 2.59 2011/11/30 12:43:51 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#include "LuaGlobals.h"
#include "LuaState.h"

#include <locale.h>
#include <string.h>

#include "lua.h"

#include "lctype.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lzio.h"



#define next(ls) (ls->current = zgetc(ls->z))



#define currIsNewline(ls)	(ls->current == '\n' || ls->current == '\r')


/* ORDER RESERVED */
const char* luaX_tokens[] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "..", "...", "==", ">=", "<=", "~=", "::", "<eof>",
    "<number>", "<name>", "<string>"
};

int luaX_tokens_count = sizeof(luaX_tokens) / sizeof(luaX_tokens[0]);


static LuaResult lexerror (LexState *ls, const char *msg, int token);


static void save (LexState *ls, int c) {
  ls->buff_.push_back((char)c);
}


const char *luaX_token2str (LexState *ls, int token) {
  THREAD_CHECK(ls->L);
  if (token < FIRST_RESERVED) {
    assert(token == cast(unsigned char, token));
    return (lisprint(token)) ? luaO_pushfstring(ls->L, LUA_QL("%c"), token) :
                              luaO_pushfstring(ls->L, "char(%d)", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS) {
      return luaO_pushfstring(ls->L, LUA_QS, s);
    }
    else {
      return s;
    }
  }
}


static const char *txtToken (LexState *ls, int token) {
  THREAD_CHECK(ls->L);
  switch (token) {
    case TK_NAME:
    case TK_STRING:
    case TK_NUMBER:
      {
        save(ls, '\0');
        return luaO_pushfstring(ls->L, LUA_QS, &ls->buff_[0]);
      }
    default:
      return luaX_token2str(ls, token);
  }
}


static LuaResult lexerror (LexState *ls, const char *msg, int token) {
  THREAD_CHECK(ls->L);
  std::string buff = luaO_chunkid2(ls->source->c_str());
  msg = luaO_pushfstring(ls->L, "%s:%d: %s", buff.c_str(), ls->linenumber, msg);
  if (token) {
    luaO_pushfstring(ls->L, "%s near %s", msg, txtToken(ls, token));
  }
  return LUA_ERRSYNTAX;
}


LuaResult luaX_syntaxerror (LexState *ls, const char *msg) {
  THREAD_CHECK(ls->L);
  return lexerror(ls, msg, ls->t.token);
}


/*
** creates a new string and anchors it in function's table so that
** it will not be collected until the end of the function's compilation
** (by that time it should be anchored in function's prototype)
*/
LuaString *luaX_newstring (LexState *ls, const char *str, size_t l) {
  THREAD_CHECK(ls->L);
  LuaThread *L = ls->L;

  LuaString* ts = thread_G->strings_->Create(str, l);  /* create new string */
  L->stack_.push_nocheck(LuaValue(ts));  /* temporarily anchor it in stack */

  // TODO(aappleby): Save string in 'ls->fs->h'. Why it does so exactly this way, I don't
  // know. Will have to investigate in the future.
  LuaValue s(ts);
  ls->fs->constant_map->set(s, LuaValue(true));

  luaC_barrierback(ls->fs->constant_map, s);

  L->stack_.pop();  /* remove string from stack */
  return ts;
}


/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
*/
static void inclinenumber (LexState *ls) {
  THREAD_CHECK(ls->L);
  int old = ls->current;
  assert(currIsNewline(ls));
  next(ls);  /* skip `\n' or `\r' */
  if (currIsNewline(ls) && ls->current != old) {
    next(ls);  /* skip `\n\r' or `\r\n' */
  }
  ls->linenumber++;
}


void luaX_setinput (LuaThread *L, LexState *ls, ZIO *z, LuaString *source, int firstchar) {
  THREAD_CHECK(L);

  ls->decpoint = '.';
  ls->L = L;
  ls->current = firstchar;
  ls->lookahead.token = TK_EOS;  /* no look-ahead token */
  ls->z = z;
  ls->fs = NULL;
  ls->linenumber = 1;
  ls->lastline = 1;
  ls->source = source;
  ls->envn = thread_G->strings_->Create(LUA_ENV);  /* create env name */
  ls->envn->setFixed();  /* never collect this name */
}



/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/



static int check_next (LexState *ls, const char *set) {
  THREAD_CHECK(ls->L);
  if (ls->current == '\0' || !strchr(set, ls->current))
    return 0;
  save(ls, ls->current);
  next(ls);
  return 1;
}


/*
** change all characters 'from' in buffer to 'to'
*/
static void buffreplace (LexState *ls, char from, char to) {
  THREAD_CHECK(ls->L);
  size_t n = ls->buff_.size();
  char *p = &ls->buff_[0];
  while (n--)
    if (p[n] == from) p[n] = to;
}


#if !defined(getlocaledecpoint)
#define getlocaledecpoint()	(localeconv()->decimal_point[0])
#endif


/*
** in case of format error, try to change decimal point separator to
** the one defined in the current locale and check again
*/
static LuaResult trydecpoint (LexState *ls, SemInfo *seminfo) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  char old = ls->decpoint;
  ls->decpoint = getlocaledecpoint();
  buffreplace(ls, old, ls->decpoint);  /* try new decimal separator */

  
  int ok = luaO_str2d(&ls->buff_[0], ls->buff_.size() - 1, &seminfo->r);
  if (!ok) {
    /* format error with correct decimal point: no more options */
    buffreplace(ls, ls->decpoint, '.');  /* undo change (for error message) */
    return lexerror(ls, "malformed number", TK_NUMBER);
  }
  return result;
}


/* double */
static LuaResult read_numeral (LexState *ls, SemInfo *seminfo) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  assert(lisdigit(ls->current));
  do {
    save(ls, ls->current);
    next(ls);
    if (check_next(ls, "EePp"))  /* exponent part? */
      check_next(ls, "+-");  /* optional exponent sign */
  } while (lislalnum(ls->current) || ls->current == '.');
  save(ls, '\0');
  buffreplace(ls, '.', ls->decpoint);  /* follow locale for decimal point */
  int ok = luaO_str2d(&ls->buff_[0], ls->buff_.size() - 1, &seminfo->r);
  if (!ok) {
    /* format error? */
    return trydecpoint(ls, seminfo); /* try to update decimal point separator */
  }
  return result;
}


/*
** skip a sequence '[=*[' or ']=*]' and return its number of '='s or
** -1 if sequence is malformed
*/
static int skip_sep (LexState *ls) {
  THREAD_CHECK(ls->L);
  int count = 0;
  int s = ls->current;
  assert(s == '[' || s == ']');
  save(ls, ls->current);
  next(ls);
  while (ls->current == '=') {
    save(ls, ls->current);
    next(ls);
    count++;
  }
  return (ls->current == s) ? count : (-count) - 1;
}


static LuaResult read_long_string (LexState *ls, SemInfo *seminfo, int sep) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  /* skip 2nd `[' */
  save(ls, ls->current);
  next(ls);
  if (currIsNewline(ls)) {
    /* string starts with a newline? */
    inclinenumber(ls);  /* skip it */
  }
  for (;;) {
    switch (ls->current) {
      case EOZ:
        return lexerror(ls, (seminfo) ? "unfinished long string" :
                                        "unfinished long comment", TK_EOS);
        
        break;  /* to avoid warnings */
      case ']': {
        if (skip_sep(ls) == sep) {
          /* skip 2nd `]' */
          save(ls, ls->current);
          next(ls);
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        if (!seminfo) ls->buff_.clear(); /* avoid wasting space */
        break;
      }
      default: {
        if (seminfo) {
          save(ls, ls->current);
          next(ls);
        }
        else next(ls);
      }
    }
  } 

endloop:

  if (seminfo) {
    seminfo->ts = luaX_newstring(ls, &ls->buff_[0] + (2 + sep),
                                     ls->buff_.size() - 2*(2 + sep));
  }
  return result;
}


static void escerror (LexState *ls, int *c, int n, const char *msg) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  int i;
  /* prepare error message */
  ls->buff_.clear();
  save(ls, '\\');
  for (i = 0; i < n && c[i] != EOZ; i++)
    save(ls, c[i]);
  result = lexerror(ls, msg, TK_STRING);
  handleResult(result);
}


static int readhexaesc (LexState *ls) {
  THREAD_CHECK(ls->L);
  int c[3], i;  /* keep input for error message */
  int r = 0;  /* result accumulator */
  c[0] = 'x';  /* for error message */
  for (i = 1; i < 3; i++) {  /* read two hexa digits */
    c[i] = next(ls);
    if (!lisxdigit(c[i]))
      escerror(ls, c, i + 1, "hexadecimal digit expected");
    r = (r << 4) + luaO_hexavalue(c[i]);
  }
  return r;
}


static int readdecesc (LexState *ls) {
  THREAD_CHECK(ls->L);
  int c[3], i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(ls->current); i++) {  /* read up to 3 digits */
    c[i] = ls->current;
    r = 10*r + c[i] - '0';
    next(ls);
  }
  if (r > UCHAR_MAX)
    escerror(ls, c, i, "decimal escape too large");
  return r;
}


static void read_string (LexState *ls, int del, SemInfo *seminfo) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  /* keep delimiter (for error messages) */
  save(ls, ls->current);
  next(ls);

  while (ls->current != del) {
    switch (ls->current) {
      case EOZ:
        result = lexerror(ls, "unfinished string", TK_EOS);
        handleResult(result);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        result = lexerror(ls, "unfinished string", TK_STRING);
        handleResult(result);
        break;  /* to avoid warnings */
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        next(ls);  /* do not save the `\' */
        switch (ls->current) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(ls); goto read_save;
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->current; goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            next(ls);  /* skip the 'z' */
            while (lisspace(ls->current)) {
              if (currIsNewline(ls)) inclinenumber(ls);
              else next(ls);
            }
            goto no_save;
          }
          default: {
            if (!lisdigit(ls->current))
              escerror(ls, &ls->current, 1, "invalid escape sequence");
            /* digital escape \ddd */
            c = readdecesc(ls);
            goto only_save;
          }
        }
       read_save: next(ls);  /* read next character */
       only_save: save(ls, c);  /* save 'c' */
       no_save: break;
      }
      default: {
        save(ls, ls->current);
        next(ls);
      }
    }
  }

  /* skip delimiter */
  save(ls, ls->current);
  next(ls);

  seminfo->ts = luaX_newstring(ls, &ls->buff_[0] + 1,
                                   ls->buff_.size() - 2);
}


static LuaResult llex (LexState *ls, SemInfo *seminfo, int& out) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  ls->buff_.clear();
  for (;;) {
    switch (ls->current) {
      case '\n': case '\r': {  /* line breaks */
        inclinenumber(ls);
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  /* spaces */
        next(ls);
        break;
      }
      case '-': {  /* '-' or '--' (comment) */
        next(ls);
        if (ls->current != '-') {
          out = '-';
          return result;
        }
        /* else is a comment */
        next(ls);
        if (ls->current == '[') {  /* long comment? */
          int sep = skip_sep(ls);
          ls->buff_.clear();  /* `skip_sep' may dirty the buffer */
          if (sep >= 0) {
            result = read_long_string(ls, NULL, sep);  /* skip long comment */
            if(result != LUA_OK) return result;
            ls->buff_.clear();  /* previous call may dirty the buff. */
            break;
          }
        }
        /* else short comment */
        while (!currIsNewline(ls) && ls->current != EOZ)
          next(ls);  /* skip until end of line (or end of file) */
        break;
      }
      case '[': {  /* long string or simply '[' */
        int sep = skip_sep(ls);
        if (sep >= 0) {
          result = read_long_string(ls, seminfo, sep);
          if(result != LUA_OK) return result;
          out = TK_STRING;
          return result;
        }
        else if (sep == -1) {
          out = '[';
          return result;
        }
        else {
          return lexerror(ls, "invalid long string delimiter", TK_STRING);
        }
      }
      case '=': {
        next(ls);
        if (ls->current != '=') {
          out = '=';
          return result;
        }
        else {
          next(ls);
          out = TK_EQ;
          return result;
        }
      }
      case '<': {
        next(ls);
        if (ls->current != '=') {
          out = '<';
          return result;
        }
        else {
          next(ls);
          out = TK_LE;
          return result;
        }
      }
      case '>': {
        next(ls);
        if (ls->current != '=') {
          out = '>';
          return result;
        }
        else {
          next(ls);
          out = TK_GE;
          return result;
        }
      }
      case '~': {
        next(ls);
        if (ls->current != '=') {
          out = '~';
          return result;
        }
        else {
          next(ls);
          out = TK_NE;
          return result;
        }
      }
      case ':': {
        next(ls);
        if (ls->current != ':') {
          out = ':';
          return result;
        }
        else {
          next(ls);
          out = TK_DBCOLON;
          return result;
        }
      }
      case '"': case '\'': {  /* short literal strings */
        read_string(ls, ls->current, seminfo);
        out = TK_STRING;
        return result;
      }
      case '.': {  /* '.', '..', '...', or number */
        save(ls, ls->current);
        next(ls);
        if (check_next(ls, ".")) {
          if (check_next(ls, ".")) {
            out = TK_DOTS;   /* '...' */
            return result;
          }
          else {
            out = TK_CONCAT;   /* '..' */
            return result;
          }
        }
        else if (!lisdigit(ls->current)) {
          out = '.';
          return result;
        }
        /* else go through */
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        result = read_numeral(ls, seminfo);
        if(result != LUA_OK) return result;
        out = TK_NUMBER;
        return result;
      }
      case EOZ: {
        out = TK_EOS;
        return result;
      }
      default: {
        if (lislalpha(ls->current)) {  /* identifier or reserved word? */
          LuaString *ts;
          do {
            save(ls, ls->current);
            next(ls);
          } while (lislalnum(ls->current));

          ts = luaX_newstring(ls, &ls->buff_[0], ls->buff_.size());

          seminfo->ts = ts;
          if (ts->getReserved() > 0) {
            /* reserved word? */
            out = ts->getReserved() - 1 + FIRST_RESERVED;
            return result;
          }
          else {
            out = TK_NAME;
            return result;
          }
        }
        else {  /* single-char tokens (+ - / ...) */
          int c = ls->current;
          next(ls);
          out = c;
          return result;
        }
      }
    }
  }
}


LuaResult luaX_next (LexState *ls) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  ls->lastline = ls->linenumber;
  if (ls->lookahead.token != TK_EOS) {  /* is there a look-ahead token? */
    ls->t = ls->lookahead;  /* use this one */
    ls->lookahead.token = TK_EOS;  /* and discharge it */
  }
  else {
    result = llex(ls, &ls->t.seminfo, ls->t.token);  /* read next token */
    if(result != LUA_OK) return result;
  }
  return result;
}


int luaX_lookahead (LexState *ls) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  assert(ls->lookahead.token == TK_EOS);
  result = llex(ls, &ls->lookahead.seminfo, ls->lookahead.token);
  handleResult(result);
  return ls->lookahead.token;
}

