// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

#define LAB8_LOCKS_1

struct run {
  struct run *next;
};

#ifndef LAB8_LOCKS_1
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;
#else
#define LOCK_NAME_N 6
struct 
{
  struct spinlock lock;
  char lock_name[LOCK_NAME_N];
  struct run *freelist;
}kmem[NCPU];
#endif

void
kinit()
{
#ifndef LAB8_LOCKS_1
  initlock(&kmem.lock, "kmem");
#else
  for(int i=0;i<NCPU;i++){
    snprintf(kmem[i].lock_name, LOCK_NAME_N, "lock%d", i);
    initlock(&kmem[i].lock, kmem[i].lock_name);
  }
#endif
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

#ifndef LAB8_LOCKS_1
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
#else
  push_off();
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  release(&kmem[cpu].lock);
  pop_off();
#endif
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
// kalloc产生死锁的情况，cpu0运行进程A，cpu1运行进程B，A和B都没有多余的内存块，都会去别的cpu上偷，这时候如果A和B都进入了for循环内，而恰好A申请B的锁看看有没有多余的内存块，
// B也在for循环内申请A的锁来借内存块，就会产生死锁。
// 所以，这里避免产生死锁的方法就是不能在持有一把锁的情况下，去申请另一把锁。也就是最多只能有一把。
void *
kalloc(void)
{
  struct run *r;

#ifndef LAB8_LOCKS_1
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
#else
  push_off();
  int cpu = cpuid();
  acquire(&kmem[cpu].lock);
  r = kmem[cpu].freelist;
  
  if(r){
    kmem[cpu].freelist=r->next;
    release(&kmem[cpu].lock);
  }else{
    release(&kmem[cpu].lock);
    for(int i=0;i<NCPU;i++){
      if(i!=cpu){
        acquire(&kmem[i].lock);
        r = kmem[i].freelist;
        if(r){
          struct run*slow=r,*fast=r;
          while(fast && fast->next){
            slow=slow->next;
            fast=fast->next->next;
          }
          kmem[i].freelist = slow->next;
          slow->next=0;
          release(&kmem[i].lock);

          acquire(&kmem[cpu].lock);
          kmem[cpu].freelist=r->next;
          r->next=0;
          release(&kmem[cpu].lock);
          break;
        }
        release(&kmem[i].lock);
      }
    }
  }
  pop_off();
#endif

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
