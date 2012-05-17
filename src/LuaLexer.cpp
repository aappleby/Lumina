#include "LuaLexer.h"

#include "LuaConversions.h"
#include "lctype.h"

#include <assert.h>

extern const char* luaX_tokens[];
extern int luaX_tokens_count;


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
    std::string temp2 = StringPrintf("%s:%d: %s near %s", buff.c_str(), linenumber_, msg, temp3.c_str());
    errors_.push_back(temp2);
  }
  else {
    std::string temp1 = StringPrintf("%s:%d: %s", buff.c_str(), linenumber_, msg);
    errors_.push_back(temp1);
  }
}
