#pragma once

#include <vector>

#define FIRST_RESERVED	257

/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum RESERVED {
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols */
  TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE, TK_DBCOLON, TK_EOS,
  TK_NUMBER, TK_NAME, TK_STRING
};

/* number of reserved words */
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))




struct Token {
 
  Token()
  : reserved_(0) {
  }

  int token;
  double r;

  void setString(const char* s, size_t len);

  int getReserved();

  const char* c_str() {
    return text_.c_str();
  }

  size_t getLen() {
    return text_.size();
  }

protected:

  std::string text_;
  int reserved_;
};


class LuaLexer {
public:

  LuaLexer() {
    buffer_.reserve(128);
  }

  void save(char c) {
    buffer_.push_back(c);
  }
  
  const char* getBuffer() const {
    return buffer_.size() ? &buffer_[0] : NULL;
  }

  void clearBuffer() {
    buffer_.clear();
  }

  size_t getLen() const {
    return buffer_.size();
  }

  void replace(char oldc, char newc) {
    for(size_t i = 0; i < buffer_.size(); i++) {
      if(buffer_[i] == oldc) buffer_[i] = newc;
    }
  }

  std::string getDebugToken(int token);

private:

  std::vector<char> buffer_;
};