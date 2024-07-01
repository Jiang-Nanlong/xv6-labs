//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
// 获取文件描述符和文件指针，成功返回0，失败返回-1
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)  // 获取文件描述符fd
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)  // 验证文件描述符的合法性
    return -1;
  if(pfd)  //如果传入的pfd不为空，就把文件描述符保存在pfd中
    *pfd = fd;
  if(pf)   //如果传入的pf不为空，就把文件指针保存在pf中
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
// 获取本进程的一个空闲的fd文件描述符，把文件指针存储在文件描述符内
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

// 将本进程中指定的文件描述符再复制一份
uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)  // 分配文件描述符fd，指向文件f
    return -1;
  filedup(f); // 增加文件的引用计数
  return fd;
}

// 根据文件描述符，读文件。文件描述符，目的地址，字节数
uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

// 写。文件描述符，源地址，字节数
uint64
sys_write(void)
{
  struct file *f; // 文件描述符
  int n;  // 字符个数
  uint64 p;  // 字符地址

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

// 关闭一个文件描述符
uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;  // 删除文件描述符fd对应的文件指针
  fileclose(f);
  return 0;
}

// 获取文件的stat信息
uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
// 把新链接的最后连接到和旧链接相同的inode。也就是硬连接。虽说最后文件名不同，但是两个路径都访问同一个inode
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){  // 如果old路径的最后是一个目录，因为不允许对目录创建硬连接
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;  // 增加一个inode的硬连接数量
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){  // 是所以要判断dp->dev和ip->dev，是因为硬连接不能跨设备
    // 最后一个文件夹下添加一个目录项，文件名还是原来的，但是inode已经变成old对应的inode
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);  // 这里减少dp和ip的引用计数是正确的。在通过namei和nameiparent获取ip和dp时，都已经在dirlookup内调用iget来增加各自的ref了

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
// 判断目录除了.和..以外是否为空
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){  // off直接从第2个目录项开始
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

// 删除目录项对inode的引用
uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)  // 不能删除.和..目录
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0) // 读取到name对应的目录项在父目录的off偏移量处，并读到name对应的inode ip
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){  // 不能删除非空目录
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))  // 把原来的目录项写入一个空的dirent
    panic("unlink: writei");
  if(ip->type == T_DIR){  // 如果要删除的path是一个目录
    dp->nlink--;  // 为什么这里要dp->nlink--??? 因为..？ 确实是因为要删除的目录中的..指向父目录，所以这里父目录的nlink要减一
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

// 创建path路径所代表的文件，文件类型为type，返回文件的inode
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)  // 获取path路径的父目录的inode dp和要创建的文件名name
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){  // 在dp下查询name是否已经存在
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))  // 如果存在同名文件，想创建的是普通文件，而且现有节点也是文件或设备文件，则直接返回当前节点
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)  // 从磁盘中分配一个新的dinode，返回对应的inode。只有当真正要写数据时，才在bmap中才真正分配block
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;  // 父目录指向这个inode
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.  如果创建的是目录，那么就给目录创建.和..两个特殊的目录项，分别连接到自身和父目录
    dp->nlink++;  // for ".." 目录中的..指向父目录的inode，所以增加父目录inode的nlink
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    // 没有ip->nlink++是为了避免自身循环引用，防止在没有父目录对它链接之后，nlink仍不为0
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)  // 在父目录下创建一个目录项，连接到刚创建的节点
    panic("create: dirlink");

  iunlockput(dp);

  return ip;  // 从这里返回的时候，并没有释放ip的锁，也没有减少从ialloc中增加的引用计数
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){  // 如果是创建一个文件
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {  // 如果该文件已存在
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){  // 如果打开的是目录，且打开类型不是只读的，就返回错误
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){ // 如果打开的是设备文件，检查它的主设备号是否有效
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){  //分配一个文件结构体和一个文件描述符
  // f是文件系统中的一个位置，fd是当前进程中打开文件列表的一个文件描述符
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){  // 根据inode类型设置文件类型
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);  
  // xv6中的O_RDONLY，O_WRONLY，O_RDWR三者是互斥的，而O_RDONLY又是000，那么就没法通过omode&O_RDONLY来判断是否可读。
  // 因为三者互斥，那么就可以通过O_WRONLY来判断，如果omode & O_WRONLY为1，则证明是可写的，取反后就是0，那么就不可读。
  // 如果omode & O_WRONLY结果为0，那么就是不可写，必定是O_RDONLY或O_RDWR中的一个，就是可读的。
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){  // 如果模式是O_TRUNC，而且类型是普通文件，那么就直接销毁
    itrunc(ip);
  }

  iunlock(ip);  // 这里开始的时候，因为如果打开模式是create的话，并没有注意到给ip上锁，后来看了create函数以后发现，从create函数返回
  // inode 的时候就已经上锁了，ip的引用计数也没有减少。
  // 如果打开模式不是create，就会从namei函数返回ip，此时ip的引用计数没有减少，但是没有上锁。
  end_op();

  return fd;
}

// 在path下创建一个目录项
uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// path下创建一个设备文件
uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// 改变当前进程的工作目录
uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);  // 释放原来的工作目录的inode，减少一个引用计数
  end_op();
  p->cwd = ip;  //更换成新的工作目录
  return 0;
}

// 利用指定的可执行文件更换当前进程的进程映像，需要和磁盘交互
uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){  // uargv是用户空间中argv数组的地址
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){  // NELEM(argv)=32
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){   // 读取用户空间argv数组的每一位元素，存储在uarg中。
    // argv数组中的每一位元素其实都是一个指向char *型的地址
      goto bad;
    }
    if(uarg == 0){  // 说明读取到了用户空间argv数组的最后
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)  // 把用户空间argv数组每一个元素对应的字符串都读取到内核空间argv[i]对应的内存空间中
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)  // 然后再循环释放前边给内核空间argv数组申请的内存空间
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
