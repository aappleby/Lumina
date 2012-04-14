#include "LuaString.h"

#include "LuaGlobals.h"

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
// TString

TString::TString(char* buf, uint32_t hash, const char* str, int len)
: LuaObject(LUA_TSTRING),
  buf_(buf),
  reserved_(0),
  hash_(hash),
  len_(len)
{
  //assert(l_memcontrol.limitDisabled);
  memcpy(buf_, str, len*sizeof(char));
  buf_[len_] = '\0'; // terminating null
}

TString::~TString() {
  luaM_free(buf_);
  buf_ = NULL;
  len_ = NULL;

  thread_G->strings_->nuse_--;
}

void TString::VisitGC(GCVisitor&) {
  setColor(GRAY);
}

int TString::PropagateGC(GCVisitor& visitor) {
  assert(false);
  return 0;
}

//-----------------------------------------------------------------------------
// Stringtable

stringtable::stringtable() {
  size_ = 0;
  nuse_ = 0;
}

stringtable::~stringtable() {
}

TString* stringtable::find(uint32_t hash, const char *str, size_t len) {

  LuaObject* o = hash_[hash & (size_-1)];

  for (; o != NULL; o = o->next_) {
    TString *ts = dynamic_cast<TString*>(o);
    if(ts->getHash() != hash) continue;
    if(ts->getLen() != len) continue;

    if (memcmp(str, ts->c_str(), len * sizeof(char)) == 0) {
      // Found a match.
      return ts;
    }
  }
  return NULL;
}

void stringtable::resize(int newsize) {
  if (newsize > size_) {
    hash_.resize_nocheck(newsize);
  }
  /* rehash */
  for (int i=0; i<size_; i++) {
    LuaObject *p = hash_[i];
    hash_[i] = NULL;
    while (p) {  /* for each node in the list */
      LuaObject *next = p->next_;  /* save next */
      unsigned int hash = dynamic_cast<TString*>(p)->getHash();
      p->next_ = hash_[hash & (newsize-1)];  /* chain it */
      hash_[hash & (newsize-1)] = p;
      p->clearOld();  /* see MOVE OLD rule */
      p = next;
    }
  }
  if (newsize < size_) {
    /* shrinking slice must be empty */
    assert(hash_[newsize] == NULL && hash_[size_ - 1] == NULL);
    hash_.resize_nocheck(newsize);
  }
  size_ = newsize;
}
