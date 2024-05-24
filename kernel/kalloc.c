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

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#ifdef LAB6_COW
struct {
  struct spinlock lock;
  uint8 refcount[(PHYSTOP-KERNBASE)/PGSIZE];
} ref;
#endif

void
kinit()
{
  initlock(&kmem.lock, "kmem");
#ifdef LAB6_COW
  initlock(&ref.lock, "ReferenceCount");
#endif
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
#ifdef LAB6_COW
    ref.refcount[((uint64)p - KERNBASE)/PGSIZE] = 1;   // 这里是因为最开始kinit内会遍历所有的内存块，然后每个内存块调用一次kfree，如果内存块的引用数一开始为0的话会报错。不过这里也可以不写，而是修改kfree内的逻辑
#endif
    kfree(p);
  }
}

#ifdef LAB6_COW
void
incref(uint64 pa){
  int pn = (pa - KERNBASE) / PGSIZE;
  acquire(&ref.lock);
  if(pa >= PHYSTOP || ref.refcount[pn] < 1)
    panic("incref error");
  ref.refcount[pn] ++;
  release(&ref.lock);
}
#endif

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

#ifdef LAB6_COW
  acquire(&ref.lock);
  int pn = ((uint64)pa - KERNBASE) / PGSIZE;
  if(ref.refcount[pn] < 1)
    panic("kfree ref error");
  ref.refcount[pn] --;
  int count = ref.refcount[pn];
  release(&ref.lock);
  
  if(count > 0)
    return;
#endif

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
#ifdef LAB6_COW
    acquire(&ref.lock);
    int pn = ((uint64) r-KERNBASE) / PGSIZE;
    if(ref.refcount[pn] != 0)
      panic("kalloc ref erroe");
    ref.refcount[pn] = 1;
    release(&ref.lock);
#endif
  }
  return (void*)r;
}
