#define LAB8_LOCKS_2
struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;    // 缓存块所在设备
  uint blockno;  // 缓存的盘块号
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list  LRU list前向指针
  struct buf *next;                   // LRU list后向指针
  uchar data[BSIZE];   // 记录缓存的盘块的数据

#ifdef LAB8_LOCKS_2
  uint last_used;
#endif
};

