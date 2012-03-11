
union TKey {
  struct {
    Value value_;
    int tt_;
    struct Node *next;  /* for chaining */
  } nk;
  TValue tvk;
};


struct Node {
  TValue i_val;
  TKey i_key;
};


class Table {
public:
  GCObject *next;
  uint8_t tt;
  uint8_t marked;
  uint8_t flags;  /* 1<<p means tagmethod(p) is not present */
  uint8_t lsizenode;  /* log2 of size of `node' array */
  Table *metatable;
  TValue *array;  /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  GCObject *gclist;
  int sizearray;  /* size of `array' array */
};