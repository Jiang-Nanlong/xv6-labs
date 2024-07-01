struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE  ����򿪵��ǹܵ�����ָ��ܵ�����
  struct inode *ip;  // FD_INODE and FD_DEVICE ָ���ڴ��е�inode
  uint off;          // FD_INODE  ��¼��ǰ�Ķ�дλ�ã�ͨ����Ϊ�ļ����α�
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count ��ʾ���ڴ�inode��ʹ�õĴ�����ʹ�����ʱҪ��ʱ����
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk? ��ʾ��inode�Ƿ��Ѿ��Ӵ����϶�ȡ���ݲ���ʼ��

  // �±߼���Ԫ����dinode�ĸ���
  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;     // ��ʾ������ָ���dinode��Ӳ���ӵ�����
  uint size;
  uint addrs[NDIRECT+1];  // ǰ12����ֱ�ӿ�ţ�ָ�򹹳��ļ���ǰ12���顣���һ���Ǽ�ӿ�ţ�ָ��һ�������Ŀ飬������ڴ洢����Ŀ�ţ����256��
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];  // devsw�����¼ÿ���豸�Ŷ�Ӧ�Ķ�д����

#define CONSOLE 1
