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
// ��ȡ�ļ����������ļ�ָ�룬�ɹ�����0��ʧ�ܷ���-1
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)  // ��ȡ�ļ�������fd
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)  // ��֤�ļ��������ĺϷ���
    return -1;
  if(pfd)  //��������pfd��Ϊ�գ��Ͱ��ļ�������������pfd��
    *pfd = fd;
  if(pf)   //��������pf��Ϊ�գ��Ͱ��ļ�ָ�뱣����pf��
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
// ��ȡ�����̵�һ�����е�fd�ļ������������ļ�ָ��洢���ļ���������
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

// ����������ָ�����ļ��������ٸ���һ��
uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)  // �����ļ�������fd��ָ���ļ�f
    return -1;
  filedup(f); // �����ļ������ü���
  return fd;
}

// �����ļ������������ļ����ļ���������Ŀ�ĵ�ַ���ֽ���
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

// д���ļ���������Դ��ַ���ֽ���
uint64
sys_write(void)
{
  struct file *f; // �ļ�������
  int n;  // �ַ�����
  uint64 p;  // �ַ���ַ

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

// �ر�һ���ļ�������
uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;  // ɾ���ļ�������fd��Ӧ���ļ�ָ��
  fileclose(f);
  return 0;
}

// ��ȡ�ļ���stat��Ϣ
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
// �������ӵ�������ӵ��;�������ͬ��inode��Ҳ����Ӳ���ӡ���˵����ļ�����ͬ����������·��������ͬһ��inode
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
  if(ip->type == T_DIR){  // ���old·���������һ��Ŀ¼����Ϊ�������Ŀ¼����Ӳ����
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;  // ����һ��inode��Ӳ��������
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){  // ������Ҫ�ж�dp->dev��ip->dev������ΪӲ���Ӳ��ܿ��豸
    // ���һ���ļ��������һ��Ŀ¼��ļ�������ԭ���ģ�����inode�Ѿ����old��Ӧ��inode
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);  // �������dp��ip�����ü�������ȷ�ġ���ͨ��namei��nameiparent��ȡip��dpʱ�����Ѿ���dirlookup�ڵ���iget�����Ӹ��Ե�ref��

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
// �ж�Ŀ¼����.��..�����Ƿ�Ϊ��
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){  // offֱ�Ӵӵ�2��Ŀ¼�ʼ
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

// ɾ��Ŀ¼���inode������
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
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)  // ����ɾ��.��..Ŀ¼
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0) // ��ȡ��name��Ӧ��Ŀ¼���ڸ�Ŀ¼��offƫ��������������name��Ӧ��inode ip
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){  // ����ɾ���ǿ�Ŀ¼
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))  // ��ԭ����Ŀ¼��д��һ���յ�dirent
    panic("unlink: writei");
  if(ip->type == T_DIR){  // ���Ҫɾ����path��һ��Ŀ¼
    dp->nlink--;  // Ϊʲô����Ҫdp->nlink--??? ��Ϊ..�� ȷʵ����ΪҪɾ����Ŀ¼�е�..ָ��Ŀ¼���������︸Ŀ¼��nlinkҪ��һ
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

// ����path·����������ļ����ļ�����Ϊtype�������ļ���inode
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)  // ��ȡpath·���ĸ�Ŀ¼��inode dp��Ҫ�������ļ���name
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){  // ��dp�²�ѯname�Ƿ��Ѿ�����
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))  // �������ͬ���ļ����봴��������ͨ�ļ����������нڵ�Ҳ���ļ����豸�ļ�����ֱ�ӷ��ص�ǰ�ڵ�
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)  // �Ӵ����з���һ���µ�dinode�����ض�Ӧ��inode��ֻ�е�����Ҫд����ʱ������bmap�в���������block
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;  // ��Ŀ¼ָ�����inode
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.  �����������Ŀ¼����ô�͸�Ŀ¼����.��..���������Ŀ¼��ֱ����ӵ�����͸�Ŀ¼
    dp->nlink++;  // for ".." Ŀ¼�е�..ָ��Ŀ¼��inode���������Ӹ�Ŀ¼inode��nlink
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    // û��ip->nlink++��Ϊ�˱�������ѭ�����ã���ֹ��û�и�Ŀ¼��������֮��nlink�Բ�Ϊ0
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)  // �ڸ�Ŀ¼�´���һ��Ŀ¼����ӵ��մ����Ľڵ�
    panic("create: dirlink");

  iunlockput(dp);

  return ip;  // �����ﷵ�ص�ʱ�򣬲�û���ͷ�ip������Ҳû�м��ٴ�ialloc�����ӵ����ü���
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

  if(omode & O_CREATE){  // ����Ǵ���һ���ļ�
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {  // ������ļ��Ѵ���
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){  // ����򿪵���Ŀ¼���Ҵ����Ͳ���ֻ���ģ��ͷ��ش���
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){ // ����򿪵����豸�ļ�������������豸���Ƿ���Ч
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){  //����һ���ļ��ṹ���һ���ļ�������
  // f���ļ�ϵͳ�е�һ��λ�ã�fd�ǵ�ǰ�����д��ļ��б��һ���ļ�������
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){  // ����inode���������ļ�����
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);  
  // xv6�е�O_RDONLY��O_WRONLY��O_RDWR�����ǻ���ģ���O_RDONLY����000����ô��û��ͨ��omode&O_RDONLY���ж��Ƿ�ɶ���
  // ��Ϊ���߻��⣬��ô�Ϳ���ͨ��O_WRONLY���жϣ����omode & O_WRONLYΪ1����֤���ǿ�д�ģ�ȡ�������0����ô�Ͳ��ɶ���
  // ���omode & O_WRONLY���Ϊ0����ô���ǲ���д���ض���O_RDONLY��O_RDWR�е�һ�������ǿɶ��ġ�
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){  // ���ģʽ��O_TRUNC��������������ͨ�ļ�����ô��ֱ������
    itrunc(ip);
  }

  iunlock(ip);  // ���￪ʼ��ʱ����Ϊ�����ģʽ��create�Ļ�����û��ע�⵽��ip��������������create�����Ժ��֣���create��������
  // inode ��ʱ����Ѿ������ˣ�ip�����ü���Ҳû�м��١�
  // �����ģʽ����create���ͻ��namei��������ip����ʱip�����ü���û�м��٣�����û��������
  end_op();

  return fd;
}

// ��path�´���һ��Ŀ¼��
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

// path�´���һ���豸�ļ�
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

// �ı䵱ǰ���̵Ĺ���Ŀ¼
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
  iput(p->cwd);  // �ͷ�ԭ���Ĺ���Ŀ¼��inode������һ�����ü���
  end_op();
  p->cwd = ip;  //�������µĹ���Ŀ¼
  return 0;
}

// ����ָ���Ŀ�ִ���ļ�������ǰ���̵Ľ���ӳ����Ҫ�ʹ��̽���
uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){  // uargv���û��ռ���argv����ĵ�ַ
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){  // NELEM(argv)=32
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){   // ��ȡ�û��ռ�argv�����ÿһλԪ�أ��洢��uarg�С�
    // argv�����е�ÿһλԪ����ʵ����һ��ָ��char *�͵ĵ�ַ
      goto bad;
    }
    if(uarg == 0){  // ˵����ȡ�����û��ռ�argv��������
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)  // ���û��ռ�argv����ÿһ��Ԫ�ض�Ӧ���ַ�������ȡ���ں˿ռ�argv[i]��Ӧ���ڴ�ռ���
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)  // Ȼ����ѭ���ͷ�ǰ�߸��ں˿ռ�argv����������ڴ�ռ�
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
