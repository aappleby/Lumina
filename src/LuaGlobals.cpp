#include "LuaGlobals.h"

#include "LuaState.h"
#include "LuaString.h"
#include "LuaTable.h"

#define LUAI_GCPAUSE	200  /* 200% */
#define LUAI_GCMAJOR	200  /* 200% */
#define LUAI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */
#define MINSTRTABSIZE	32

#define MEMERRMSG       "not enough memory"

extern const char* luaX_tokens[];
extern int luaX_tokens_count;

const double *lua_version (LuaThread *L);

void luaC_freeallobjects();

/* ORDER TM */
const char *const gk_tagmethod_names[] = {
  "__index",
  "__newindex",
  "__gc",
  "__mode",
  "__len",
  "__eq",
  "__add",
  "__sub",
  "__mul",
  "__div",
  "__mod",
  "__pow",
  "__unm",
  "__lt",
  "__le",
  "__concat",
  "__call"
};

//------------------------------------------------------------------------------

LuaObject** getGlobalGCHead() {
  return &thread_G->allgc;
}

LuaVM::LuaVM()
: uvhead(NULL)
{
  LuaVM* oldVM = thread_G;
  LuaThread* oldThread = thread_L;

  thread_G = NULL;
  thread_L = NULL;

  GCdebt_ = 0;
  totalbytes_ = sizeof(LuaVM);
  call_depth_ = 0;

  livecolor = LuaObject::colorA;
  deadcolor = LuaObject::colorB;

  uvhead.uprev = &uvhead;
  uvhead.unext = &uvhead;

  gcstate = GCSpause;
  gckind = KGC_NORMAL;
  gcrunning = 0;  /* no GC while building state */
  gcpause = LUAI_GCPAUSE;
  gcmajorinc = LUAI_GCMAJOR;
  gcstepmul = LUAI_GCMUL;
  lastmajormem = 0;

  allgc = NULL;
  sweepgc = NULL;
  //finobj = NULL;

  panic = NULL;
  version = lua_version(NULL);

  anchor_head_ = NULL;
  anchor_tail_ = NULL;

  memset(base_metatables_,0,sizeof(base_metatables_));
  memset(instanceCounts,0,sizeof(instanceCounts));

  // Make this global state the active one in this thread
  thread_G = this;

  // Create the main thread
  mainthread = new LuaThread(this);
  thread_L = mainthread;

  // Create global registry.
  LuaTable* registry = new LuaTable(LUA_RIDX_LAST, 0);
  l_registry = LuaValue(registry);

  // Create global variable table.
  LuaTable* globals = new LuaTable();
  registry->set(LuaValue(LUA_RIDX_GLOBALS), LuaValue(globals));
  l_globals = LuaValue(globals);

  // Store main thread in the registry.
  // TODO(aappleby): There is no reason to keep it there, except that there is
  // one test in api.lua that checks for it.
  registry->set(LuaValue(LUA_RIDX_MAINTHREAD), LuaValue(mainthread));

  // Create global string table.
  strings_ = new LuaStringTable();
  strings_->Resize(MINSTRTABSIZE);  /* initial size of string table */

  // Create memory error message string.
  memerrmsg = strings_->Create(MEMERRMSG);
  memerrmsg->setFixed();

  // Create tagmethod name strings.
  memset(tagmethod_names_,0,sizeof(tagmethod_names_));
  int tm_count = sizeof(gk_tagmethod_names) / sizeof(gk_tagmethod_names[0]);
  for (int i=0; i < tm_count; i++) {
    tagmethod_names_[i] = strings_->Create(gk_tagmethod_names[i]);
    tagmethod_names_[i]->setFixed();
  }

  // Create lexer reserved word strings.
  for (int i = 0; i < luaX_tokens_count; i++) {
    LuaString *ts = strings_->Create(luaX_tokens[i]);
    ts->setFixed();
    ts->setReserved(i+1);
  }

  // Store global table in global table. Why?
  globals->set("_G", LuaValue(globals) );

  // Store version string in global table.
  LuaString* version = strings_->Create(LUA_VERSION);
  globals->set("_VERSION", LuaValue(version) );

  // Global state has been created, start up the garbage collector.
  gcrunning = 1;

  thread_L = oldThread;
  thread_G = oldVM;
}

//------------------------------------------------------------------------------

LuaVM::~LuaVM() {
  LuaVM* oldVM = thread_G;
  LuaThread* oldThread = thread_L;

  thread_G = this;
  thread_L = mainthread;

  gc_.ClearGraylists();

  luaC_freeallobjects();  /* collect all objects */

  delete mainthread;
  mainthread = NULL;
  thread_L = NULL;

  delete strings_;
  strings_ = NULL;

  buff.buffer.clear();

  assert(getTotalBytes() == sizeof(LuaVM));
  assert(mainthread == NULL);
  assert(anchor_head_ == NULL);
  assert(anchor_tail_ == NULL);

  thread_L = oldThread;
  thread_G = oldVM;
}

//------------------------------------------------------------------------------

void LuaVM::setGCDebt(size_t debt) {
  GCdebt_ = debt;
}

void LuaVM::incTotalBytes(int bytes) {
  totalbytes_ += bytes;
}

void LuaVM::incGCDebt(int debt) { 
  GCdebt_ += debt;
}

//------------------------------------------------------------------------------

LuaTable* LuaVM::getRegistryTable(const char* name) {
  LuaTable* registry = getRegistry();

  LuaValue val = registry->get(name);

  if(val.isTable()) return val.getTable();

  LuaTable* newTable = new LuaTable();
  registry->set(name, newTable);
  return newTable;
}

//------------------------------------------------------------------------------

