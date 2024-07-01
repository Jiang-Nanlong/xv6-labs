struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE  如果打开的是管道，则指向管道数据
  struct inode *ip;  // FD_INODE and FD_DEVICE 指向内存中的inode
  uint off;          // FD_INODE  记录当前的读写位置，通常作为文件的游标
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count 表示改内存inode被使用的次数，使用完成时要及时减少
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk? 表示该inode是否已经从磁盘上读取数据并初始化

  // 下边几个元素是dinode的副本
  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;     // 表示磁盘上指向该dinode的硬连接的数量
  uint size;
  uint addrs[NDIRECT+1];  // 前12个是直接块号，指向构成文件的前12个块。最后一个是间接块号，指向一个单独的块，这个块内存储其余的块号，最多256个
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];  // devsw数组记录每个设备号对应的读写函数

#define CONSOLE 1
