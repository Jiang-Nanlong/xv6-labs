// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

void
acquiresleep(struct sleeplock *lk)  // 普通的锁上锁后会关中断，而睡眠锁不会。即使在获取睡眠锁的过程中会调用acquire短暂关中断，但是进入sleep之后，shed函数调用之前又会重新开中断
{   // 所以sleeplock不会关中断，并且可以长期持有
  acquire(&lk->lk);  // 这里申请睡眠锁的自旋锁是为了处理lk->locked的竞争问题
  while (lk->locked) {
    sleep(lk, &lk->lk);  // 如果当前睡眠锁被占用，则睡眠当前进程
  }
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk);
}

void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk);
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}



