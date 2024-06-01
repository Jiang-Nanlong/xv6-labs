// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13

struct {
  struct spinlock lock;   // 保留这个锁，后边用作防止一个block有两个缓存块的情况发生
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
#ifndef LAB8_LOCKS_2
  struct buf head;   // 这是一个头节点，什么都不保存
#else
  struct buf bucket[NBUCKET];
  struct spinlock bucket_lock[NBUCKET];
#endif
} bcache;

#ifdef LAB8_LOCKS_2
int
hash(uint blockno)
{
  return blockno % NBUCKET;
}
#endif

void
binit(void)   // buf[NBUF]中的buf结构体，连起来构成一个环形的双向链表
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
#ifdef LAB8_LOCKS_2
  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache.bucket_lock[i], "bucket_lock");
  }
#endif
#ifndef LAB8_LOCKS_2
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
#else
  for(int i = 0; i < NBUCKET; i++) {
    bcache.bucket[i].next = &bcache.bucket[i];
    bcache.bucket[i].prev = &bcache.bucket[i];
  }
#endif
  
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
#ifndef LAB8_LOCKS_2
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
#else
    int key = hash(b->blockno);
    b->next = bcache.bucket[key].next;
    b->prev = &bcache.bucket[key];
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[key].next->prev = b;
    bcache.bucket[key].next = b;
    
    b->last_used = ticks;
#endif
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
#ifndef LAB8_LOCKS_2

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
#else
  int key = hash(blockno);
  acquire(&bcache.bucket_lock[key]);

  // Is the block already cached?
  for(b = bcache.bucket[key].next; b != &bcache.bucket[key]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.bucket_lock[key]);
      acquiresleep(&b->lock);  // 申请缓冲块的锁，如果获得该锁就运行，否则就睡眠，并把当前进程加入到该锁的等待队列。releasesleep函数中会调用wakeup来唤醒等待队列中的下一个进程
      return b;
    }
  }
  release(&bcache.bucket_lock[key]);

  acquire(&bcache.lock);  // 为了防止两个cpu上运行同样的程序，使用同样的磁盘块，同样在找合适的缓冲块进行分配，不满足一个磁盘块只能有一个缓冲块的规定。加了这个大锁保证只有一个cpu可以运行分配缓冲块的程序
  acquire(&bcache.bucket_lock[key]);
  for(b = bcache.bucket[key].next; b != &bcache.bucket[key]; b = b->next) { 
    // 这里又重新走了一遍上边的流程是为了防止这种情况：cpu0和cpu1都运行这个函数，且参数都相同，也就是都想访问一个磁盘块。cpu0获取了bcache.lock，cpu1同样运行到这里，
    // 此时此刻说明两个cpu上运行的程序都已经探测到缺少该磁盘块对应的缓冲块了。因为cpu0获得了bcache.lock，所以他会给该磁盘块找到一个对应的缓冲块，并操作这个缓冲块，cpu0运行完释放了bcache.lock，然后cpu1就可以运行了。
    // 如果cpu1在这里不重新走一遍上边的流程，而是直接查找空闲缓冲块分配给磁盘块的话，就会导致该磁盘块有两个对应的缓冲块，这是不允许的，所以为了防止这种情况发生，此处需要再走一遍探测磁盘块对应缓冲块的流程。
    if(b->dev == dev && b->blockno == blockno) {  
      b->refcnt++;
      release(&bcache.bucket_lock[key]);
      release(&bcache.lock);
      acquiresleep(&b->lock);  // 申请缓冲块的锁，如果获得该锁就运行，否则就睡眠，并把当前进程加入到该锁的等待队列。releasesleep函数中会调用wakeup来唤醒等待队列中的下一个进程
      return b;
    }
  }
  release(&bcache.bucket_lock[key]);

  struct buf *found = 0;
  uint min_ticks = ~0;  // 记录磁盘块最近最小的访问时间

  for(int i = 0; i < NBUCKET; i++) {
    acquire(&bcache.bucket_lock[i]);
    int find = 0;  // 用来判断在第i个桶上是否找到了更LRU的缓存块，因为如果找到的话，就不用在本层释放该桶的锁而是在下一次找到b->last_used更小的时候释放本层的锁；如果没找到，就在本层最后释放锁
    for(b = bcache.bucket[i].next; b != &bcache.bucket[i]; b = b->next){
      if(b->refcnt == 0 && b->last_used < min_ticks) {
        if(found){
          int last = hash(found->blockno);
          if(last != i)  // 有可能在同一个桶上找到多个合适的缓冲块，这样的话，在遇到更合适的缓冲块的时候就不用释放之前的锁了
            release(&bcache.bucket_lock[last]);
        }
        found = b;
        min_ticks = b->last_used;
        find = 1;
      }
    }
    if(find == 0)  // 如果在本桶上没找到合适的块，就释放本桶的锁
      release(&bcache.bucket_lock[i]);
  }

  if(found == 0)
    panic("bget: no buffers");

  int last_used_index = hash(found->blockno);
  found->dev = dev;
  found->blockno = blockno;
  found->valid = 0;
  found->refcnt = 1;

  if(last_used_index != key){ // 如果新找到的缓冲块和key所在的桶不是同一个桶，就要把新找到缓冲块从原来的桶中摘除
    found->prev->next = found->next;
    found->next->prev = found->prev;
  }
  release(&bcache.bucket_lock[last_used_index]);  // 摘除以后就可以释放新找的块所在的桶的锁了

  if(last_used_index != key){  //如果新找到的缓冲块和key所在的桶不是同一个桶，就要把新找到缓冲块插入到key所在的桶中
    acquire(&bcache.bucket_lock[key]);
    found->next = bcache.bucket[key].next;
    found->prev = &bcache.bucket[key];
    bcache.bucket[key].next->prev = found;
    bcache.bucket[key].next = found;
    release(&bcache.bucket_lock[key]);
  }
  release(&bcache.lock);
  acquiresleep(&found->lock);
  return found;
#endif
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)   // 从磁盘中读取一块放入缓冲区
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)     // 把缓冲区的内容写入磁盘块
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)   // 释放一块缓冲区
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
#ifndef LAB8_LOCKS_2
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {    // 把要释放的缓冲块放在远离head的那一头
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
#else
  int key = hash(b->blockno);
  acquire(&bcache.bucket_lock[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->last_used = ticks;
  }
  release(&bcache.bucket_lock[key]);
#endif
}

void
bpin(struct buf *b) {
#ifndef LAB8_LOCKS_2
  acquire(&bcache.lock);
#else
  int key = hash(b->blockno);
  acquire(&bcache.bucket_lock[key]);
#endif
  b->refcnt++;
#ifndef LAB8_LOCKS_2
  release(&bcache.lock);
#else
  release(&bcache.bucket_lock[key]);
#endif
}

void
bunpin(struct buf *b) {
#ifndef LAB8_LOCKS_2
  acquire(&bcache.lock);
#else
  int key = hash(b->blockno);
  acquire(&bcache.bucket_lock[key]);
#endif
  b->refcnt--;
#ifndef LAB8_LOCKS_2
  release(&bcache.lock);
#else
  release(&bcache.bucket_lock[key]);
#endif
}

