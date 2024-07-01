#define T_DIR     1   // Directory Ŀ¼
#define T_FILE    2   // File  ��ͨ�ļ�
#define T_DEVICE  3   // Device  �豸�ļ�

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};
