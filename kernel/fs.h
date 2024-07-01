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
  uint size;         // Size of file system image (blocks) �ļ�ϵͳ�Ĵ�С���Կ�Ϊ��λ
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
  uint nlog;         // Number of log blocks
  uint logstart;     // Block number of first log block ��һ����־�����ڵĿ��
  uint inodestart;   // Block number of first inode block ��һ��inode���ڵĿ��
  uint bmapstart;    // Block number of first free map block ��һ��λͼ����
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))  // ���Ŀ¼���ж��ٸ��̿�ţ�256��
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {   // �ܹ�64�ֽ�
  short type;           // File type
  short major;          // Major device number (T_DEVICE only)
  short minor;          // Minor device number (T_DEVICE only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses  ǰ12����ֱ�ӿ�ţ�ָ�򹹳��ļ���ǰ12���顣���һ���Ǽ�ӿ�ţ�ָ��һ�������Ŀ飬������ڴ洢����Ŀ�ţ����256��
};

// Inodes per block.
// ÿ��block���ж���inode
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
//�����i��inodeλ���ĸ���
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
// һ��block�ж���bitmap bitλ
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
// �����b�����Ӧ��bitmapλ���ĸ���
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
// Ŀ¼�����ļ����ĳ���
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

