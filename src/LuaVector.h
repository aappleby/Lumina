#pragma once

#include <algorithm>
#include "lmem.h"

l_noret luaD_throw (int errcode);

template<class T>
class LuaVector {
public:

  LuaVector()
  : buf(NULL),
    size(0)
  {
  }

  void init()
  {
    buf = NULL;
    size = 0;
  }

  ~LuaVector()
  {
    clear();
  }
  
  T& operator [] ( int index ) 
  {
    assert(index >= 0);
    assert(index < size);
    return buf[index];
  }

  const T& operator [] ( int index ) const
  {
    assert(index >= 0);
    assert(index < size);
    return buf[index];
  }

  void resize ( int newsize )
  {
    if(newsize == 0)
    {
      clear();
      return;
    }
    T* newbuf = reinterpret_cast<T*>(default_alloc(sizeof(T) * newsize, 0));
    if(newbuf == NULL) luaD_throw(LUA_ERRMEM);;
    memcpy(newbuf, buf, sizeof(T) * std::min(size,newsize));
    default_free(buf, sizeof(T) * size, 0);
    buf = newbuf;
    size = newsize;
  }

  void clear ( void )
  {
    default_free(buf, sizeof(T) * size, 0);
    buf = NULL;
    size = 0;
  }

  void grow() 
  {
    resize(size ? size * 2 : 16);
  }

  bool empty() 
  {
    return (size == 0);
  }


  T* buf;
  int size;
};