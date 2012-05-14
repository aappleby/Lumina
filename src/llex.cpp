/*
** $Id: llex.c,v 2.59 2011/11/30 12:43:51 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#include "LuaConversions.h"
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



//#define next(ls) (ls->current = zgetc(ls->z))



#define currIsNewline(ls)	(ls->current_ == '\n' || ls->current_ == '\r')


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


static LuaResult lexerror (LexState *ls, const char *msg, int token, ErrorList& errors);


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


static LuaResult lexerror (LexState *ls, const char *msg, int token, ErrorList& errors) {
  THREAD_CHECK(ls->L);
  std::string buff = luaO_chunkid2(ls->source->c_str());
  std::string temp1 = StringPrintf("%s:%d: %s", buff.c_str(), ls->linenumber, msg);
  errors.push_back(temp1);

  if (token) {
    std::string temp2 = StringPrintf("%s near %s", temp1.c_str(), txtToken(ls, token));
    errors.push_back(temp2);
  }

  return LUA_ERRSYNTAX;
}


LuaResult luaX_syntaxerror (LexState *ls, const char *msg) {
  THREAD_CHECK(ls->L);
  LuaResult result = lexerror(ls, msg, ls->t.token, ls->L->errors_);
  ls->L->PushErrors();
  return result;
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
  int old = ls->current_;
  assert(currIsNewline(ls));
  ls->current_ = ls->z->getc();  /* skip `\n' or `\r' */
  if (currIsNewline(ls) && ls->current_ != old) {
    ls->current_ = ls->z->getc();  /* skip `\n\r' or `\r\n' */
  }
  ls->linenumber++;
}


void luaX_setinput (LuaThread *L, LexState *ls, Zio *z, LuaString *source, int firstchar) {
  THREAD_CHECK(L);

  ls->decpoint = '.';
  ls->L = L;
  ls->current_ = firstchar;
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
  if (ls->current_ == '\0' || !strchr(set, ls->current_))
    return 0;
  save(ls, ls->current_);
  ls->current_ = ls->z->getc();
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
static LuaResult trydecpoint (LexState *ls, Token* token) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  char old = ls->decpoint;
  ls->decpoint = getlocaledecpoint();
  buffreplace(ls, old, ls->decpoint);  /* try new decimal separator */

  
  int ok = luaO_str2d(&ls->buff_[0], ls->buff_.size() - 1, &token->r);
  if (!ok) {
    /* format error with correct decimal point: no more options */
    buffreplace(ls, ls->decpoint, '.');  /* undo change (for error message) */
    result = lexerror(ls, "malformed number", TK_NUMBER, ls->L->errors_);
    ls->L->PushErrors();
    return result;
  }
  return result;
}


/* double */
static LuaResult read_numeral (LexState *ls, Token* token) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  assert(lisdigit(ls->current_));
  do {
    save(ls, ls->current_);
    ls->current_ = ls->z->getc();
    if (check_next(ls, "EePp")) {  /* exponent part? */
      check_next(ls, "+-");  /* optional exponent sign */
    }
  } while (lislalnum(ls->current_) || ls->current_ == '.');
  save(ls, '\0');
  buffreplace(ls, '.', ls->decpoint);  /* follow locale for decimal point */
  int ok = luaO_str2d(&ls->buff_[0], ls->buff_.size() - 1, &token->r);
  if (!ok) {
    /* format error? */
    return trydecpoint(ls, token); /* try to update decimal point separator */
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
  int s = ls->current_;
  assert(s == '[' || s == ']');
  save(ls, ls->current_);
  ls->current_ = ls->z->getc();
  while (ls->current_ == '=') {
    save(ls, ls->current_);
    ls->current_ = ls->z->getc();
    count++;
  }
  return (ls->current_ == s) ? count : (-count) - 1;
}


static LuaResult read_long_string (LexState *ls, Token* token, int sep) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  /* skip 2nd `[' */
  save(ls, ls->current_);
  ls->current_ = ls->z->getc();
  if (currIsNewline(ls)) {
    /* string starts with a newline? */
    inclinenumber(ls);  /* skip it */
  }
  for (;;) {
    switch (ls->current_) {
      case EOZ:
        result = lexerror(ls, (token) ? "unfinished long string" :
                                        "unfinished long comment", TK_EOS, ls->L->errors_);
        ls->L->PushErrors();
        return result;
        break;  /* to avoid warnings */
      case ']': {
        if (skip_sep(ls) == sep) {
          /* skip 2nd `]' */
          save(ls, ls->current_);
          ls->current_ = ls->z->getc();
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        if (!token) ls->buff_.clear(); /* avoid wasting space */
        break;
      }
      default: {
        if (token) {
          save(ls, ls->current_);
          ls->current_ = ls->z->getc();
        }
        else {
          ls->current_ = ls->z->getc();
        }
      }
    }
  } 

endloop:

  if (token) {
    token->ts = luaX_newstring(ls,
                               &ls->buff_[0] + (2 + sep),
                               ls->buff_.size() - 2*(2 + sep));
  }
  return result;
}


static LuaResult escerror (LexState *ls, int *c, int n, const char *msg) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  int i;
  /* prepare error message */
  ls->buff_.clear();
  save(ls, '\\');
  for (i = 0; i < n && c[i] != EOZ; i++) {
    save(ls, c[i]);
  }
  result = lexerror(ls, msg, TK_STRING, ls->L->errors_);
  ls->L->PushErrors();
  return result;
}


static LuaResult readhexaesc (LexState *ls, int& out) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  int c[3], i;  /* keep input for error message */
  int r = 0;  /* result accumulator */
  c[0] = 'x';  /* for error message */
  for (i = 1; i < 3; i++) {  /* read two hexa digits */
    c[i] = ls->current_ = ls->z->getc();
    if (!lisxdigit(c[i])) {
      result = escerror(ls, c, i + 1, "hexadecimal digit expected");
      if(result != LUA_OK) return result;
    }
    r = (r << 4) + luaO_hexavalue(c[i]);
  }
  out = r;
  return result;
}


static int readdecesc (LexState *ls) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  int c[3], i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(ls->current_); i++) {  /* read up to 3 digits */
    c[i] = ls->current_;
    r = 10*r + c[i] - '0';
    ls->current_ = ls->z->getc();
  }
  if (r > UCHAR_MAX) {
    result = escerror(ls, c, i, "decimal escape too large");
    handleResult(result);
  }
  return r;
}


static void read_string (LexState *ls, int del, Token* token) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  /* keep delimiter (for error messages) */
  save(ls, ls->current_);
  ls->current_ = ls->z->getc();

  while (ls->current_ != del) {
    switch (ls->current_) {
      case EOZ:
        result = lexerror(ls, "unfinished string", TK_EOS, ls->L->errors_);
        ls->L->PushErrors();
        handleResult(result);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        result = lexerror(ls, "unfinished string", TK_STRING, ls->L->errors_);
        ls->L->PushErrors();
        handleResult(result);
        break;  /* to avoid warnings */
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        ls->current_ = ls->z->getc();  /* do not save the `\' */
        switch (ls->current_) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': {
            result = readhexaesc(ls, c);
            handleResult(result);
            goto read_save;
          }
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->current_; goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            ls->current_ = ls->z->getc();  /* skip the 'z' */
            while (lisspace(ls->current_)) {
              if (currIsNewline(ls)) inclinenumber(ls);
              else {
                ls->current_ = ls->z->getc();
              }
            }
            goto no_save;
          }
          default: {
            if (!lisdigit(ls->current_)) {
              result = escerror(ls, &ls->current_, 1, "invalid escape sequence");
              handleResult(result);
            }
            /* digital escape \ddd */
            c = readdecesc(ls);
            goto only_save;
          }
        }
       read_save: ls->current_ = ls->z->getc();  /* read next character */
       only_save: save(ls, c);  /* save 'c' */
       no_save: break;
      }
      default: {
        save(ls, ls->current_);
        ls->current_ = ls->z->getc();
      }
    }
  }

  /* skip delimiter */
  save(ls, ls->current_);
  ls->current_ = ls->z->getc();

  token->ts = luaX_newstring(ls,
                             &ls->buff_[0] + 1,
                             ls->buff_.size() - 2);
}


static LuaResult llex (LexState *ls, Token* out) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  ls->buff_.clear();
  for (;;) {
    switch (ls->current_) {
      case '\n': case '\r': {  /* line breaks */
        inclinenumber(ls);
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  /* spaces */
        ls->current_ = ls->z->getc();
        break;
      }
      case '-': {  /* '-' or '--' (comment) */
        ls->current_ = ls->z->getc();
        if (ls->current_ != '-') {
          out->token = '-';
          return result;
        }
        /* else is a comment */
        ls->current_ = ls->z->getc();
        if (ls->current_ == '[') {  /* long comment? */
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
        while (!currIsNewline(ls) && ls->current_ != EOZ) {
          ls->current_ = ls->z->getc();  /* skip until end of line (or end of file) */
        }
        break;
      }
      case '[': {  /* long string or simply '[' */
        int sep = skip_sep(ls);
        if (sep >= 0) {
          result = read_long_string(ls, out, sep);
          if(result != LUA_OK) return result;
          out->token = TK_STRING;
          return result;
        }
        else if (sep == -1) {
          out->token = '[';
          return result;
        }
        else {
          result = lexerror(ls, "invalid long string delimiter", TK_STRING, ls->L->errors_);
          ls->L->PushErrors();
          return result;
        }
      }
      case '=': {
        ls->current_ = ls->z->getc();
        if (ls->current_ != '=') {
          out->token = '=';
          return result;
        }
        else {
          ls->current_ = ls->z->getc();
          out->token = TK_EQ;
          return result;
        }
      }
      case '<': {
        ls->current_ = ls->z->getc();
        if (ls->current_ != '=') {
          out->token = '<';
          return result;
        }
        else {
          ls->current_ = ls->z->getc();
          out->token = TK_LE;
          return result;
        }
      }
      case '>': {
        ls->current_ = ls->z->getc();
        if (ls->current_ != '=') {
          out->token = '>';
          return result;
        }
        else {
          ls->current_ = ls->z->getc();
          out->token = TK_GE;
          return result;
        }
      }
      case '~': {
        ls->current_ = ls->z->getc();
        if (ls->current_ != '=') {
          out->token = '~';
          return result;
        }
        else {
          ls->current_ = ls->z->getc();
          out->token = TK_NE;
          return result;
        }
      }
      case ':': {
        ls->current_ = ls->z->getc();
        if (ls->current_ != ':') {
          out->token = ':';
          return result;
        }
        else {
          ls->current_ = ls->z->getc();
          out->token = TK_DBCOLON;
          return result;
        }
      }
      case '"': case '\'': {  /* short literal strings */
        read_string(ls, ls->current_, out);
        out->token = TK_STRING;
        return result;
      }
      case '.': {  /* '.', '..', '...', or number */
        save(ls, ls->current_);
        ls->current_ = ls->z->getc();
        if (check_next(ls, ".")) {
          if (check_next(ls, ".")) {
            out->token = TK_DOTS;   /* '...' */
            return result;
          }
          else {
            out->token = TK_CONCAT;   /* '..' */
            return result;
          }
        }
        else if (!lisdigit(ls->current_)) {
          out->token = '.';
          return result;
        }
        /* else go through */
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        result = read_numeral(ls, out);
        if(result != LUA_OK) return result;
        out->token = TK_NUMBER;
        return result;
      }
      case EOZ: {
        out->token = TK_EOS;
        return result;
      }
      default: {
        if (lislalpha(ls->current_)) {  /* identifier or reserved word? */
          LuaString *ts;
          do {
            save(ls, ls->current_);
            ls->current_ = ls->z->getc();
          } while (lislalnum(ls->current_));

          ts = luaX_newstring(ls, &ls->buff_[0], ls->buff_.size());

          out->ts = ts;
          if (ts->getReserved() > 0) {
            /* reserved word? */
            out->token = ts->getReserved() - 1 + FIRST_RESERVED;
            return result;
          }
          else {
            out->token = TK_NAME;
            return result;
          }
        }
        else {  /* single-char tokens (+ - / ...) */
          int c = ls->current_;
          ls->current_ = ls->z->getc();
          out->token = c;
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
    result = llex(ls, &ls->t);  /* read next token */
    if(result != LUA_OK) return result;
  }
  return result;
}


int luaX_lookahead (LexState *ls) {
  LuaResult result = LUA_OK;
  THREAD_CHECK(ls->L);
  assert(ls->lookahead.token == TK_EOS);
  result = llex(ls, &ls->lookahead);
  handleResult(result);
  return ls->lookahead.token;
}

