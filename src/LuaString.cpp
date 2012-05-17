#include "LuaString.h"

#include "LuaGlobals.h"

#include "llimits.h"

//-----------------------------------------------------------------------------

uint32_t hashString(const char* str, size_t len) {
  uint32_t hash = (uint32_t)len;

  size_t step = (len>>5)+1;  // if string is too long, don't hash all its chars
  for (size_t l1 = len; l1 >= step; l1 -= step) {
    hash = hash ^ ((hash<<5)+(hash>>2) + (unsigned char)str[l1-1]);
  }

  return hash;
}

//-----------------------------------------------------------------------------
// LuaString

LuaString::LuaString(uint32_t hash, const char* str, int len)
: LuaObject(LUA_TSTRING),
  buf_(NULL),
  reserved_(0),
  hash_(hash),
  len_(len)
{
  buf_ = (char*)luaM_alloc_nocheck(len_+1);
  memcpy(buf_, str, len*sizeof(char));
  buf_[len_] = '\0'; // terminating null
}

LuaString::~LuaString() {
  luaM_free(buf_);
  buf_ = NULL;
  len_ = NULL;
}

//-----------------------------------------------------------------------------

void LuaString::VisitGC(LuaGCVisitor& v) {
  v.VisitString(this);
}

int LuaString::PropagateGC(LuaGCVisitor&) {
  assert(false);
  return 0;
}

//-----------------------------------------------------------------------------
// Stringtable

LuaStringTable::LuaStringTable() {
  nuse_ = 0;
}

LuaStringTable::~LuaStringTable() {
}

LuaString* LuaStringTable::find(uint32_t hash, const char *str, size_t len) {
  LuaList& l = hash_[hash & (hash_.size()-1)];

  for(LuaList::iterator it = l.begin(); it; ++it) {
    LuaString *ts = dynamic_cast<LuaString*>(it.get());
    if(ts->getHash() != hash) continue;
    if(ts->getLen() != len) continue;

    if (memcmp(str, ts->c_str(), len * sizeof(char)) == 0) {
      // Found a match.
      return ts;
    }
  }
  return NULL;
}

//-----------------------------------------------------------------------------

void LuaStringTable::Resize(int newsize) {
  int oldsize = (int)hash_.size();

  LuaVector<LuaList> newhash;
  newhash.resize_nocheck(newsize);

  /* rehash */
  for (int i=0; i < oldsize; i++) {
    LuaList& oldlist = hash_[i];

    while (!oldlist.isEmpty()) {
      LuaString* s = (LuaString*)oldlist.Pop();
      unsigned int hash = s->getHash();

      LuaList& newlist = newhash[hash & (newsize-1)];
      newlist.Push(s);

      s->clearOld();  /* see MOVE OLD rule */
    }
  }

  hash_.swap(newhash);

  sweepCursor_ = 0;
}

//-----------------------------------------------------------------------------

LuaString* LuaStringTable::Create(const std::string& str) {
  return Create(str.c_str(), str.size());
}

LuaString* LuaStringTable::Create(const char* str) {
  return Create(str, strlen(str));
}

LuaString* LuaStringTable::Create(const char *str, int len) {
  uint32_t hash = hashString(str,len);

  LuaString* old_string = find(hash, str, len);
  if(old_string) {
    if(old_string->isDead()) old_string->makeLive();
    return old_string;
  }

  if ((nuse_ >= (uint32_t)hash_.size()) && (hash_.size() <= MAX_INT/2)) {
    Resize(hash_.size() * 2);
  }
  
  LuaString* new_string = new LuaString(hash, str, len);

  LuaList& list = hash_[hash & (hash_.size() - 1)];
  new_string->linkGC(list);
  nuse_++;
  return new_string;
}

//-----------------------------------------------------------------------------

bool LuaStringTable::Sweep(bool generational) {

  if(sweepCursor_ >= (int)hash_.size()) sweepCursor_ = 0;

  LuaList::iterator it = hash_[sweepCursor_].begin();

  while(it) {
    if (it->isDead()) {
      LuaObject* dead = it;
      it.pop();
      delete dead;
      nuse_--;
    }
    else {
      if(generational) {
        if (it->isOld()) break;
        it->setOld();
      }
      else {
        it->makeLive();
      }
      ++it;
    }
  }

  sweepCursor_++;

  return sweepCursor_ == (int)hash_.size();
}

//-----------------------------------------------------------------------------

void LuaStringTable::Shrink() {
  if (nuse_ < (uint32_t)(hash_.size() / 2)) {
    Resize(hash_.size() / 2);
  }
}

void LuaStringTable::RestartSweep() {
  sweepCursor_ = 0;
}

//-----------------------------------------------------------------------------

void LuaStringTable::Clear() {

  for(int i = 0; i < (int)hash_.size(); i++) {
    LuaList& l = hash_[i];

    while(!l.isEmpty()) {
      LuaObject* dead = l.Pop();
      delete dead;
    }
  }

  nuse_ = 0;
  sweepCursor_ = 0;
}

//-----------------------------------------------------------------------------
