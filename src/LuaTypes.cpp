#include "LuaTypes.h"

__declspec(thread) lua_State* thread_L = NULL;
__declspec(thread) global_State* thread_G = NULL;
