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
// �����ϵ�log header
struct logheader {
  int n;  // ��¼Ҫ�޸ĵ��ļ���
  int block[LOGSIZE];  // ��¼Ҫ�޸ĵ��̿���̿��
};

struct log {
  struct spinlock lock;
  int start;  // log����һ���̿�Ŀ��
  int size;   // log�����̿���
  int outstanding; // ��¼��ǰ������begin_op������û�е���end_op��������һ��outstanding���0��Ҳ�ͱ�ʾ��������ύ��
  int committing;  // ���һ����־�Ƿ����ڽ����ύ����
  int dev;
  struct logheader lh;
};
struct log log;   // �ڴ��е�log�ṹ��

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
  recover_from_log();  // �ʼ�Ȼָ�һ����־
}

// Copy committed blocks from log to their home location
// ���ݴ�log��ת��ʵ�ʵ��̿飬����ǻ����ڴ��е�log header��
static void
install_trans(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // �ӵ�2��log�鿪ʼ����ȡn��log��
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // ��ȡ��Ҫд����̿飬��Ϊlog_write���������������ü��������Դ��̿�Ļ����������ᱻ�������ﻹ�ǻ�ӻ�������ȡ
    memmove(dbuf->data, lbuf->data, BSIZE);  // ��log�����̿�����д�뵽Ӧ��д��Ĵ��̿�
    bwrite(dbuf);  // write dst to disk
    bunpin(dbuf);  // ��Ϊdbuf�Ѿ�д����̣����Լ���һ���Ըû��������ü��������ö�Ӧlog_write�����е�bpin����
    // �����һ����룬Ϊʲô������ǰ��write_log�����ڼ������ü������Ͼ���write_log���Ѿ��ѻ�����д��log���ˡ�
    // �������֣�������������д������֮ǰ�������ü��������п�����ɻ���������ǰ����
    // Ȼ�󻺳����ͱ�����д�ش��̣��ƻ���log���Ƶ�ԭ���ԡ�
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
// ��ȡ�����е�log header�洢���ڴ���log�ṹ����
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
// ���ڴ��е�log header����д�����
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
  bwrite(buf);  //�������ύ�㣬ֻҪ��һ����ɣ�log��Ϳ���д��������ʵ��Ҫд��λ��
  brelse(buf);
}

// ��־�ָ��������ȴӴ��̶�ȡlog header��ʼ�����ݵ�log�ṹ�壬Ȼ���ύ�������º���д����log header�Դ����е���������
static void
recover_from_log(void)
{
  read_head();
  install_trans(); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
// ����ʼ����
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){  // �����ǰ��һ����־�����ύ���򲻿�ʼ�µ���־��������commit�ڼ䣬log.committing����1
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // �����ٿ�������Ѿ�����
      // ��ʱ�Ѿ���log.lh.n��δ�ύ������ͬʱ����log.outstanding������û�е���end_op���ټ��ϱ�������һ��log.outstanding+1��
      // һ���������дMAXOPBLOCKS����־������һ��log.lh.n+(log.outstanding+1)*MAXOPBLOCKS
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
  log.outstanding -= 1;  // log.outstanding��1����begin_op�еļ�1��Ӧ
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){  // ���log.outstandingΪ0����ô��˵�����е�begin_op������end_op���Ϳ����ύ������
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
// ���޸��˵Ĵ��̿�����д�뵽log������Ϊxv6�޸Ĵ������ݶ����޸Ļ�����ڵ����ݣ�bread��ȡ���ǻ��������ݣ�Ҳ�����޸����˵ġ�
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // ��ȡ���������ݣ�Ҳ�����޸����˵�����
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
    write_log();     // write_log��write_head��˳���ܵߵ����ߵ����������֮������ᵼ����־ȱʧ��
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
// ��һ����Ҫ�޸ĵĴ��̿���ӵ���ǰ������ڴ�log header
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
  if (i == log.lh.n) {  // Add new block to log? ������Ҫд�Ĵ��̺Ų�����log header��
    bpin(b);   // �Ѹû������̶���buffer��
    // �������ύʱ,install_trans������Ҫ����־����ȡ�޸ĺ������,�����临�Ƶ��ļ�ϵͳ��ʵ�ʴ��̿���,
    // Ϊ�˷�ֹ�����������,��صĻ������������ڴ�,xv6���뱣֤��Щ�������������ڴ��С��������ü���������ʱ�Ե���ֹ�û��������ͷŻ��߻���
    log.lh.n++;
  }
  release(&log.lock);
}

