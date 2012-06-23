#include "LuaLexer.h"

#include "LuaConversions.h"
#include "LuaLog.h"
#include "lctype.h"

#include <assert.h>

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

void Token::setString(const char* s, size_t len) {
  text_ = std::string(s,len);
  reserved_ = 0;
  // TODO(aappleby): Searching this list for every token will probably slow the lexer down...
  for(int i = 0; i < luaX_tokens_count; i++) {
    if(strcmp(text_.c_str(), luaX_tokens[i]) == 0) {
      reserved_ = i+1;
      break;
    }
  }
}

void Token::setString2(const char* s, size_t len) {
  text_ = std::string(s,len);
  reserved_ = 0;
}

int Token::getReserved() {
  return reserved_;
}

// Converts the current token into a debug-output-friendly form.
std::string LuaLexer::getDebugToken(int token) {
  if((token == TK_NAME) || (token == TK_STRING) || (token == TK_NUMBER)) {
      std::string temp(buffer_.begin(), buffer_.end());
      return StringPrintf("'%s'", temp.c_str());
  }

  if (token < FIRST_RESERVED) {
    assert(token == (unsigned char)token);
    return (lisprint(token)) ? StringPrintf("'%c'", token) :
                               StringPrintf("char(%d)", token);
  }
  else {
    if (token < TK_EOS) {
      return StringPrintf("'%s'", luaX_tokens[token - FIRST_RESERVED]);
    }
    else {
      return luaX_tokens[token - FIRST_RESERVED];
    }
  }
}

void LuaLexer::RecordLexError(const char *msg, int token) {
  std::string buff = luaO_chunkid2(source_.c_str());

  if (token) {
    std::string temp3 = getDebugToken(token);
    log_->RecordError("%s:%d: %s near %s", buff.c_str(), linenumber_, msg, temp3.c_str());
  }
  else {
    log_->RecordError("%s:%d: %s", buff.c_str(), linenumber_, msg);
  }
}
