// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC 用于识别文件是否为 ELF 格式
  uchar elf[12];  // 存储了 ELF 文件的识别信息
  ushort type;  // ELF 文件的类型
  ushort machine;
  uint version;
  uint64 entry;  // 存储了程序入口点的虚拟地址
  uint64 phoff;  // 存储了program header在文件中的偏移量
  uint64 shoff;  // 存储了section header在文件中的偏移量
  uint flags;  // 存储了一些 ELF 文件的标志位
  ushort ehsize;  // 存储了 ELF 头部结构体本身的大小
  ushort phentsize;  // 表示一条program header条目的大小
  ushort phnum;  // 存储了program header的项数
  ushort shentsize;  // 存储了一条section header条目的大小
  ushort shnum;  // 表示section header的数量
  ushort shstrndx;  // 表示包含节名字符串的节在节头表中的索引
};

// Program section header，可执行目标文件只需要用到program header table，怪不得这里只有program header的定义，而没有section header
struct proghdr {
  uint32 type;
  uint32 flags;  //该segment的权限，一个segment内的section都拥有相同的权限
  uint64 off;    //segment第一个字节相对于文件开头的偏移量
  uint64 vaddr;  // 该segment加载到内存中的虚地址
  uint64 paddr;
  uint64 filesz;  //segment在文件中的大小，即segment占用文件的字节数
  uint64 memsz;   //segment在内存中的大小，即segment在进程地址空间中的大小。一般来说，filesz <= memsz，中间的差值使用0来填充
  uint64 align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
