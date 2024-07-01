#define T_DIR     1   // Directory 目录
#define T_FILE    2   // File  普通文件
#define T_DEVICE  3   // Device  设备文件

struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
};
