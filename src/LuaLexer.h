#pragma once

#include <assert.h>
#include <vector>

#define FIRST_RESERVED	257

class LuaLog;

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

  Token& operator = (RESERVED id) {
    assert(id >= 0);
    id_ = id;
  }

  Token& operator = (const char c) {
    assert(c >= 0);
    id_ = c;
  }

  Token& operator = (const unsigned char c) {
    assert(c >= 0);
    id_ = c;
  }

  Token& operator = (double number) {
    id_ = TK_NUMBER;
    number_ = number;
  }

  void setString(const char* s, size_t len);

  // doesn't check for reserved words
  void setString2(const char* s, size_t len);

  int getReserved();

  const char* c_str() {
    return text_.c_str();
  }

  size_t getLen() {
    return text_.size();
  }

  int getId() const { return id_; }
  double getNumber() const { return number_; }

protected:

  int id_;
  double number_;

  std::string text_;
  int reserved_;
};


class LuaLexer {
public:

  LuaLexer(LuaLog* log) : log_(log) {
    Reset(NULL);
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

  int getLineNumber() const {
    return linenumber_;
  }

  void incLineNumber() {
    linenumber_++;
  }

  void Reset(const char* source) {
    if(source) {
      source_ = source;
    }
    else {
      source_.clear();
    }
    buffer_.clear();
    buffer_.reserve(128);
    linenumber_ = 1;
  }
  
  const std::string& getSource() {
    return source_;
  }

  void RecordLexError(const char* msg, int token);

  //Token& getToken() { return token_; }

private:

  std::string source_;

  std::vector<char> buffer_;
  int linenumber_;  /* input line counter */

  LuaLog* log_;

  //Token token_;
};