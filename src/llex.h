/*
** $Id: llex.h,v 1.72 2011/11/30 12:43:51 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "LuaLexer.h"

#include<vector>

#include "lobject.h"
#include "lzio.h"


class LexState;

class FuncState;

/* state of the lexer plus state of the parser when shared by all
   functions */
class LexState {
public:

  LexState() {
  }

  int current_;  /* current character (charint) */
  int lastline;  /* line of last token `consumed' */

  Token t;  /* current token */
  Token lookahead;  /* look ahead token */
  FuncState *fs;  /* current function (parser) */
  LuaThread *L;
  Zio *z;  /* input stream */

  struct Dyndata *dyd;  /* dynamic structures used by the parser */
  LuaString *envn;  /* environment variable name */
  char decpoint;  /* locale decimal point */

  LuaLexer lexer_;
};


void luaX_setinput (LuaThread *L, LexState *ls, Zio *z,
                              LuaString *source, int firstchar);
LuaString *luaX_newstring (LexState *ls, const char *str, size_t l);
LuaResult luaX_next (LexState *ls);
LuaResult luaX_lookahead (LexState *ls, int& out);
LuaResult luaX_syntaxerror (LexState *ls, const char *s);
const char *luaX_token2str (LexState *ls, int token);


#endif
