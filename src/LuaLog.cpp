#include "LuaLog.h"

#include "LuaConversions.h"

void LuaLog::RecordError(const char* fmt, ...) {
  va_list argp;
  va_start(argp, fmt);
  std::string error;
  StringVprintf(fmt, argp, error);
  errors_.push_back(error);
  va_end(argp);
}