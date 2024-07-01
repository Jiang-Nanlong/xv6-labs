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
  // �ѳ�����ص��ڴ���ȥ������elf�ļ���ʽ������Ӧ��ֻ���õ�program header table
  // �±ߵ�ѭ���ǰ��û��ռ��е�text��data���Ƶ����ڴ���ȥ���۲��ӡ����ҳ�������֪����text��data�Ƿ���һ��ģ���û�����ֿ�����
  // ��ô�����Ҫ��Ϊ��exec������ʵ��
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
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)   //Ϊ�½��̷��������ڴ棬����ҳ���ϵ�ӳ���ϵ
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)  //����loadseg�����ѳ���μ��ص��½��̵��ڴ��е���program header.vaddr��ָ����λ��
      goto bad;
  }
  iunlockput(ip);
  end_op();  // ������־����
  ip = 0;

  p = myproc();  //p�����»ص���ǰ����
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack. ����Ҫ��xv6���û��ڴ�ռ�ķ��������ǰ�ߵĲ���ֻ������text��data
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-2*PGSIZE);   //����guard page��ע�⿴uvmclear������guard page��~PTE_U�ģ�Ҳ�����û������޷�����
  sp = sz;
  stackbase = sp - PGSIZE; //����stack����ʼλ�ã�stack�Ǵ��ϵ�������������guard page��stack�·������ջ��������guard pageȴ�û������޷�����guard page�ͻᴥ��page fault

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) { //exec��������һ���µĽ���ʱ�õڶ��������滻��һ�������ͷ��������forѭ���ڣ��õڶ��������滻��һ�������Ƶ��û��ڴ��stack��
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;  //�û��ڴ��е�stack�б�����̵����в����������ǴӸ������ַ���������ַ����
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;  //ustack�����ŵ��Ǻ��������������ַ
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);  //�ٽ�ÿ�������ĵ�ַ�洢��stack�У����������˳�����Ǵӵ͵�ַ���ߵ�ַ���е���
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)  //���������ʣ�Ϊʲô�õ���copyout���漰�����ں���Ϣ���Ƶ��û��ڴ���
  // �ֿ���һ�£��о����ﻹ���漰���ˣ�����exec����ʱ�����ں�̬������������Ҫ�ؽ�һ�����̵��ڴ�ռ䣬���̵��ڴ�ռ�һ���Ǵ����û�̬�ģ����������漰����copyout
  // ��89��Ҳ�õ���copyout
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;  //a1�Ĵ����� RISC-V �ܹ�����������ź����ĵڶ���������
  // ������ ����ط������ٿ����������Ǹ�ɶ��

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')  //��¼���һ��/����һ��λ��
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image. �ύ�û����̾���
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main //elf.entryָ������ڣ�ͨ��λ��.text���ڣ������ⲿ������ҳ��;�ҳ����һ���ġ�
  // �޸ĵ�ǰ���̵�trapframe->epc����ǰ���̴��ں�̬�����û�̬ʱ���Ὺʼִ���³������ڵ�ַ
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
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)  // �������segment���ص��ڴ������ַva�У������va������ҳ�����
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
