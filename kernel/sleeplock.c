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
acquiresleep(struct sleeplock *lk)  // ��ͨ�������������жϣ���˯�������ᡣ��ʹ�ڻ�ȡ˯�����Ĺ����л����acquire���ݹ��жϣ����ǽ���sleep֮��shed��������֮ǰ�ֻ����¿��ж�
{   // ����sleeplock������жϣ����ҿ��Գ��ڳ���
  acquire(&lk->lk);  // ��������˯��������������Ϊ�˴���lk->locked�ľ�������
  while (lk->locked) {
    sleep(lk, &lk->lk);  // �����ǰ˯������ռ�ã���˯�ߵ�ǰ����
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



