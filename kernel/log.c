#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
// 磁盘上的log header
struct logheader {
  int n;  // 记录要修改的文件数
  int block[LOGSIZE];  // 记录要修改的盘块的盘块号
};

struct log {
  struct spinlock lock;
  int start;  // log区第一个盘块的块号
  int size;   // log区的盘块数
  int outstanding; // 记录当前调用了begin_op，但是没有调用end_op的数量，一旦outstanding变成0，也就表示事务可以提交了
  int committing;  // 标记一批日志是否正在进行提交操作
  int dev;
  struct logheader lh;
};
struct log log;   // 内存中的log结构体

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.size = sb->nlog;
  log.dev = dev;
  recover_from_log();  // 最开始先恢复一遍日志
}

// Copy committed blocks from log to their home location
// 数据从log区转到实际的盘块，这个是基于内存中的log header的
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // 从第2个log块开始，读取n个log块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // 读取将要写入的盘块，因为log_write函数中增加了引用计数，所以磁盘块的缓冲区并不会被驱逐，这里还是会从缓冲区读取
    memmove(dbuf->data, lbuf->data, BSIZE);  // 把log区的盘块内容写入到应该写入的磁盘块
    bwrite(dbuf);  // write dst to disk
    bunpin(dbuf);  // 因为dbuf已经写入磁盘，所以减少一个对该缓冲块的引用计数，正好对应log_write函数中的bpin函数
    // 这里我还在想，为什么不能提前到write_log函数内减少引用计数，毕竟在write_log内已经把缓冲区写到log区了。
    // 后来发现，在真正把数据写到磁盘之前减少引用计数，都有可能造成缓冲区被提前驱逐，
    // 然后缓冲区就被单独写回磁盘，破坏了log机制的原子性。
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
// 读取磁盘中的log header存储到内存中log结构体中
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
// 把内存中的log header内容写入磁盘
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);  //真正的提交点，只要这一步完成，log块就可以写到磁盘上实际要写的位置
  brelse(buf);
}

// 日志恢复函数，先从磁盘读取log header初始化内容的log结构体，然后提交事务，完事后，再写磁盘log header对磁盘中的事务清零
static void
recover_from_log(void)
{
  read_head();
  install_trans(); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
// 事务开始函数
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){  // 如果当前有一批日志正在提交，则不开始新的日志。在整个commit期间，log.committing都是1
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // 后来再看这里，我已经懂了
      // 此时已经有log.lh.n条未提交的事务，同时还有log.outstanding条事务还没有调用end_op，再加上本次事务，一共log.outstanding+1条
      // 一条事务最多写MAXOPBLOCKS条日志，所以一共log.lh.n+(log.outstanding+1)*MAXOPBLOCKS
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;  // log.outstanding减1，和begin_op中的加1对应
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){  // 如果log.outstanding为0，那么就说明所有的begin_op都有了end_op，就可以提交事务了
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
// 把修改了的磁盘块数据写入到log区。因为xv6修改磁盘数据都是修改缓冲块内的数据，bread读取的是缓冲块的数据，也就是修改完了的。
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // 读取缓冲块的数据，也就是修改完了的数据
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // write_log和write_head的顺序不能颠倒，颠倒后如果两者之间崩溃会导致日志缺失。
    write_head();    // Write header to disk -- the real commit
    install_trans(); // Now install writes to home locations
    log.lh.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
// 把一个需要修改的磁盘块添加到当前事务的内存log header
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // log absorbtion
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // Add new block to log? 如果这次要写的磁盘号并不在log header中
    bpin(b);   // 把该缓冲区固定在buffer中
    // 在事务提交时,install_trans函数需要从日志区读取修改后的数据,并将其复制到文件系统的实际磁盘块中,
    // 为了防止在这个过程中,相关的缓冲区被换出内存,xv6必须保证这些缓冲区存在于内存中。增加引用计数可以暂时性地阻止该缓冲区被释放或者换出
    log.lh.n++;
  }
  release(&log.lock);
}

