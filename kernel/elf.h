// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC ����ʶ���ļ��Ƿ�Ϊ ELF ��ʽ
  uchar elf[12];  // �洢�� ELF �ļ���ʶ����Ϣ
  ushort type;  // ELF �ļ�������
  ushort machine;
  uint version;
  uint64 entry;  // �洢�˳�����ڵ�������ַ
  uint64 phoff;  // �洢��program header���ļ��е�ƫ����
  uint64 shoff;  // �洢��section header���ļ��е�ƫ����
  uint flags;  // �洢��һЩ ELF �ļ��ı�־λ
  ushort ehsize;  // �洢�� ELF ͷ���ṹ�屾��Ĵ�С
  ushort phentsize;  // ��ʾһ��program header��Ŀ�Ĵ�С
  ushort phnum;  // �洢��program header������
  ushort shentsize;  // �洢��һ��section header��Ŀ�Ĵ�С
  ushort shnum;  // ��ʾsection header������
  ushort shstrndx;  // ��ʾ���������ַ����Ľ��ڽ�ͷ���е�����
};

// Program section header����ִ��Ŀ���ļ�ֻ��Ҫ�õ�program header table���ֲ�������ֻ��program header�Ķ��壬��û��section header
struct proghdr {
  uint32 type;
  uint32 flags;  //��segment��Ȩ�ޣ�һ��segment�ڵ�section��ӵ����ͬ��Ȩ��
  uint64 off;    //segment��һ���ֽ�������ļ���ͷ��ƫ����
  uint64 vaddr;  // ��segment���ص��ڴ��е����ַ
  uint64 paddr;
  uint64 filesz;  //segment���ļ��еĴ�С����segmentռ���ļ����ֽ���
  uint64 memsz;   //segment���ڴ��еĴ�С����segment�ڽ��̵�ַ�ռ��еĴ�С��һ����˵��filesz <= memsz���м�Ĳ�ֵʹ��0�����
  uint64 align;
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
