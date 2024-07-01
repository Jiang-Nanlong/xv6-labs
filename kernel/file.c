//
// Support functions for system calls that involve file descriptors.
// file.c主要涉及文件系统中关于file层面的操作，向上提供系统调用的接口，向下衔接inode和磁盘层面的操作

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];  //devsw[i]封装了可以对一个设备施加的所有操作，NDEV是xv6中的最大设备号，值为10
//这表明在Xv6内部最多只支持注册10种不同设备的驱动程序(事实上只定义了console一种)，且每一种设备只支持读写两种操作
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;  // 系统打开的文件列表

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
// 在ftable.file数组中分配一个空闲的file，返回文件指针
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
// 增加文件的引用计数
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
// 关闭一个文件，
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  // 运行到这说明file.ref等于0了，但是inode的ref并不一定等于0，所以需要iput操作减少一个对inode的引用计数
  ff = *f;  // 把*f的内容复制到ff中
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){  // 如果原类型是FD_PIPE，则调用pipeclose关闭管道
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){  // 如果原类型是FD_INODE或FD_DEVICE，减少一个inode引用计数
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
// 读取文件f的stat数据并拷贝到addr，成功返回0，失败返回-1
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
// 从文件描述符f对应的文件中读取n个字符到虚拟地址addr中
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)  // 先判断是否可读
    return -1;

  if(f->type == FD_PIPE){  // 管道类型
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){   // 如果是设备文件
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){  // 如果是inode
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
// 根据传入的文件描述符的类型选择不同的写操作
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)  // 先判断是否可写
    return -1;

  if(f->type == FD_PIPE){  // 如果是管道
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){  // 如果是设备文件
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){  // 如果是inode
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;  // 计算一次最多写入多少字节
    int i = 0;
    while(i < n){  // 使用while循环，把n个字节分批次写入
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

