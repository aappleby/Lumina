#pragma once

#include <vector>
#include <string>

class LuaLog {
public:

  const std::vector<std::string>& getErrors() {
    return errors_;
  }

  void clearErrors() {
    errors_.clear();
  }

  void RecordError(const char* fmt, ...);

protected:

  std::vector<std::string> errors_;
};