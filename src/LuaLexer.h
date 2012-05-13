#pragma once

#include <vector>

class LuaLexer {
public:

  LuaLexer() {
    buffer_.reserve(128);
  }

  void save(char c) {
    buffer_.push_back(c);
  }
  
  const char* getBuffer() const {
    return &buffer_[0];
  }

  void replace(char oldc, char newc) {
    for(size_t i = 0; i < buffer_.size(); i++) {
      if(buffer_[i] == oldc) buffer_[i] = newc;
    }
  }

private:

  std::vector<char> buffer_;
};