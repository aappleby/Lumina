#pragma once

#include <vector>

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

private:

  std::vector<char> buffer_;
};