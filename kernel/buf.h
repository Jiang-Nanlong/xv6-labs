struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;    // 缓存块所在设备
  uint blockno;  // 缓存的盘块号
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];   // 1024字节，保存整个盘块的数据
};

