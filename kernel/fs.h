// On-disk file system format.
// Both the kernel and user programs use this header file.


#define ROOTINO  1   // root i-number
#define BSIZE 1024  // block size

// Disk layout:
// [ boot block | super block | log | inode blocks | 
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint magic;        // Must be FSMAGIC
  uint size;         // Size of file system image (blocks) 文件系统的大小，以块为单位
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block 第一个日志块所在的块号
  uint inodestart;   // Block number of first inode block 第一个inode所在的块号
  uint bmapstart;    // Block number of first free map block 第一个位图块块号
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))  // 间接目录中有多少个盘块号，256个
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {   // 总共64字节
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses  前12个是直接块号，指向构成文件的前12个块。最后一个是间接块号，指向一个单独的块，这个块内存储其余的块号，最多256个
};

// Inodes per block.
// 每个block上有多少inode
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
//计算第i个inode位于哪个块
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
// 一个block有多少bitmap bit位
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
// 计算第b个块对应的bitmap位于哪个块
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
// 目录项中文件名的长度
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

