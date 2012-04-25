#include "LuaString.h"

#include "LuaGlobals.h"

#include "llimits.h"

//-----------------------------------------------------------------------------

uint32_t hashString(const char* str, size_t len) {
  uint32_t hash = (uint32_t)len;

  size_t step = (len>>5)+1;  // if string is too long, don't hash all its chars
  size_t l1;
  for (l1 = len; l1 >= step; l1 -= step) {
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

void LuaString::VisitGC(LuaGCVisitor&) {
  setColor(GRAY);
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
  LuaObject* o = hash_[hash & (hash_.size()-1)];

  for (; o != NULL; o = o->next_) {
    LuaString *ts = dynamic_cast<LuaString*>(o);
    if(ts->getHash() != hash) continue;
    if(ts->getLen() != len) continue;

    if (memcmp(str, ts->c_str(), len * sizeof(char)) == 0) {
      // Found a match.
      return ts;
    }
  }
  return NULL;
}

void LuaStringTable::Resize(int newsize) {
  assert(l_memcontrol.limitDisabled);

  int oldsize = (int)hash_.size();

  if (newsize > oldsize) {
    hash_.resize_nocheck(newsize);
  }
  /* rehash */
  for (int i=0; i < oldsize; i++) {
    LuaString *p = hash_[i];
    hash_[i] = NULL;
    while (p) {  /* for each node in the list */
      LuaString *next = (LuaString*)p->next_;  /* save next */
      unsigned int hash = dynamic_cast<LuaString*>(p)->getHash();
      p->next_ = hash_[hash & (newsize-1)];  /* chain it */
      hash_[hash & (newsize-1)] = p;
      p->clearOld();  /* see MOVE OLD rule */
      p = next;
    }
  }
  if (newsize < oldsize) {
    /* shrinking slice must be empty */
    assert(hash_[newsize] == NULL && hash_[oldsize - 1] == NULL);
    hash_.resize_nocheck(newsize);
  }

  sweepCursor_ = 0;
}

//-----------------------------------------------------------------------------

LuaString* LuaStringTable::Create( const char* str )
{
  assert(l_memcontrol.limitDisabled);
  return Create(str, strlen(str));
}

LuaString* LuaStringTable::Create(const char *str, int len) {
  assert(l_memcontrol.limitDisabled);
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

  LuaString** list = &hash_[hash & (hash_.size() - 1)];
  new_string->linkGC((LuaObject**)list);
  nuse_++;
  return new_string;
}

//-----------------------------------------------------------------------------

bool LuaStringTable::Sweep(bool generational) {

  if(sweepCursor_ >= (int)hash_.size()) sweepCursor_ = 0;

  LuaString** cursor = &hash_[sweepCursor_];

  while(*cursor) {
    LuaObject* s = *cursor;
    if (s->isDead()) {
      *cursor = (LuaString*)s->next_;
      delete s;
      nuse_--;
    }
    else {
      if(generational) {
        if (s->isOld()) break;
        s->setOld();
      }
      else {
        s->makeLive();
      }
      cursor = (LuaString**)&s->next_;
    }
  }

  sweepCursor_++;

  return sweepCursor_ == (int)hash_.size();
}

//-----------------------------------------------------------------------------

void LuaStringTable::Shrink() {
  ScopedMemChecker c;
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
    while(hash_[i]) {
      LuaObject* dead = hash_[i];
      hash_[i] = (LuaString*)hash_[i]->next_;
      delete dead;
    }
  }

  nuse_ = 0;
  sweepCursor_ = 0;
}

//-----------------------------------------------------------------------------
