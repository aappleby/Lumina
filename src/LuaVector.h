#pragma once
#include "LuaTypes.h"
#include "LuaDefines.h"
#include <algorithm>
#include <assert.h>

#include "lmem.h"

l_noret luaD_throw (int errcode);

template<class T>
class LuaVector {
public:

  LuaVector()
  : buf_(NULL),
    size_(0)
  {
  }

  ~LuaVector()
  {
    clear();
  }
  
  T& operator [] ( size_t index ) 
  {
    assert(index >= 0);
    assert(index < size_);
    return buf_[index];
  }

  const T& operator [] ( size_t index ) const
  {
    assert(index >= 0);
    assert(index < size_);
    return buf_[index];
  }

  bool resize_nothrow ( size_t newsize )
  {
    if(newsize == 0)
    {
      clear();
      return true;
    }
    void* blob = luaM_alloc(sizeof(T) * newsize);
    if(blob == NULL) return false;
    T* newbuf = reinterpret_cast<T*>(blob);
    if(size_) {
      memcpy(newbuf, buf_, sizeof(T) * std::min(size_,newsize));
      luaM_free(buf_);
    }
    if(newsize > size_) {
      memset(&newbuf[size_], 0 , sizeof(T) * (newsize - size_));
    }
    buf_ = newbuf;
    size_ = newsize;
    return true;
  }

  bool resize_nocheck ( size_t newsize )
  {
    if(newsize == 0)
    {
      clear();
      return true;
    }
    void* blob = luaM_alloc_nocheck(sizeof(T) * newsize);
    if(blob == NULL) return false;
    T* newbuf = reinterpret_cast<T*>(blob);
    if(size_) {
      memcpy(newbuf, buf_, sizeof(T) * std::min(size_,newsize));
      luaM_free(buf_);
    }
    if(newsize > size_) {
      memset(&newbuf[size_], 0 , sizeof(T) * (newsize - size_));
    }
    buf_ = newbuf;
    size_ = newsize;
    return true;
  }

  void resize ( size_t newsize )
  {
    /*
    if(!l_memcontrol.limitEnabled) {
      if(!resize_nothrow(newsize)) {
        luaD_throw(LUA_ERRMEM);
      }
      return;
    }

    if(l_memcontrol.isOverLimit()) {
      luaD_throw(LUA_ERRMEM);
    }

    l_memcontrol.disableLimit();

    if(!resize_nothrow(newsize)) {
      luaD_throw(LUA_ERRMEM);
    }

    l_memcontrol.enableLimit();

    if(l_memcontrol.isOverLimit()) {
      luaD_throw(LUA_ERRMEM);
    }
    */
    if(!resize_nothrow(newsize)) {
      luaD_throw(LUA_ERRMEM);
    }
  }

  void clear ( void )
  {
    if(size_) luaM_free(buf_);
    buf_ = NULL;
    size_ = 0;
  }

  void grow() 
  {
    resize(size_ ? size_ * 2 : 16);
  }

  bool empty() const
  {
    return (size_ == 0);
  }
  
  size_t size() const { return size_; }

  const T* begin() const { return &buf_[0]; }
  T* begin() { return &buf_[0]; }

  const T* end() const { return begin() + size_; }
  T* end() { return begin() + size_; }

  void swap ( LuaVector<T>& v ) {
    T* tempbuf = v.buf_;
    size_t tempsize = v.size_;
    v.buf_ = buf_;
    v.size_ = size_;
    buf_ = tempbuf;
    size_ = tempsize;
  }

  T* buf_;
  size_t size_;
};