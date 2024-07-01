#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory. 
  // 把程序加载到内存中去，根据elf文件格式，这里应该只会用到program header table
  // 下边的循环是把用户空间中的text和data复制到新内存中去，观察打印出的页表项可以知道，text和data是放在一起的，并没有区分开来，
  // 这么设计主要是为了exec函数好实现
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)   //为新进程分配物理内存，建立页表上的映射关系
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)  //调用loadseg函数把程序段加载到新进程的内存中的由program header.vaddr所指定的位置
      goto bad;
  }
  iunlockput(ip);
  end_op();  // 结束日志操作
  ip = 0;

  p = myproc();  //p又重新回到当前进程
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack. 这里要看xv6中用户内存空间的分配情况，前边的步骤只复制了text和data
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);   //设置guard page，注意看uvmclear函数，guard page是~PTE_U的，也就是用户程序无法访问
  sp = sz;
  stackbase = sp - PGSIZE; //设置stack的起始位置，stack是从上到下增长，所以guard page在stack下方，如果栈增长到了guard page却用户进程无法访问guard page就会触发page fault

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) { //exec函数生成一个新的进程时用第二个参数替换第一个参数就发生在这个for循环内，用第二个参数替换第一个，复制到用户内存的stack中
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;  //用户内存中的stack中保存进程的运行参数，参数是从高虚拟地址往低虚拟地址排列
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;  //ustack数组存放的是函数参数的虚拟地址
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);  //再将每个参数的地址存储到stack中，这里的排列顺序又是从低地址到高地址排列的了
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)  //这里有疑问，为什么用到了copyout，涉及到将内核信息复制到用户内存吗？
  // 又看了一下，感觉这里还真涉及到了，运行exec函数时处于内核态，而我们是想要重建一个进程的内存空间，进程的内存空间一定是处于用户态的，所以这里涉及到了copyout
  // 第89行也用到了copyout
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;  //a1寄存器在 RISC-V 架构中是用来存放函数的第二个参数的
  // ？？？ 这个地方后来再看，看不懂是干啥了

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')  //记录最后一个/的下一个位置
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image. 提交用户进程镜像
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main //elf.entry指向函数入口，通常位于.text段内，所以这部分在新页表和旧页表是一样的。
  // 修改当前进程的trapframe->epc，当前进程从内核态返回用户态时，会开始执行新程序的入口地址
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)  // 将程序的segment加载到内存虚拟地址va中，这里的va必须是页对齐的
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
