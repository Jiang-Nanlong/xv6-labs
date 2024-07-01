//
// Console input and output, to the uart.
// Reads are line at a time.
// Implements special input characters:
//   newline -- end of line
//   control-h -- backspace
//   control-u -- kill line
//   control-d -- end of file
//   control-p -- print process list
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

//
// send one character to the uart.
// called by printf, and to echo input characters,
// but not from write().
//
void
consputc(int c)
{
  if(c == BACKSPACE){  // 当c是退格键
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b'); // '\b'是将光标回退一格，然后用空格覆盖要退格的字符，然后再回退光标
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;  // 自旋锁
  
  // input
#define INPUT_BUF 128
  char buf[INPUT_BUF];  // console内部的环形缓冲区
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;  // 表示控制台的信息与状态

//
// user write()s to the console go here.
// 通过sys_write函数跳转到这里，此时的user_src是1
int
consolewrite(int user_src, uint64 src, int n)  //从src读取n个字符写到console，user_src指示src是用户地址还是内核地址
{
  int i;

  acquire(&cons.lock);
  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)  // 从虚拟地址src中读取一个字符存放到c
      break;
    uartputc(c);  // 把读到的字符c存放入UART的缓冲区，并且异步显示
  }
  release(&cons.lock);

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
// 类似于consolewrite函数，consoleread也是通过sys_read函数跳转到这里
int
consoleread(int user_dst, uint64 dst, int n)  // 从console读取n字符或一行到dst中，user_dst指示dst是用户地址还是内核地址
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){  // 缓冲区内没有字符，休眠等待
      if(myproc()->killed){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF];  // 从缓冲区读取一个字符

    if(c == C('D')){  // end-of-file  判断是否输入了Ctrl + D
      if(n < target){  // 如果已经读取了一些字符
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--; // 回退读指针，将Ctrl+D保存到下一次读取操作
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;  //这里用cbuf替代c是因为copyout函数内会改变cbuf的指针的地址
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)  // 从cbuf读取一个字符到用户空间虚拟地址dst
      break;

    dst++;
    --n;

    if(c == '\n'){ //整行读取结束，退出本次读取
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;  // 返回本次读取实际读取的字符数量
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)   // 把字符放入console的缓冲区cons.buf中
{
  acquire(&cons.lock);

  switch(c){
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('U'):  // Kill line.
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f':
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF){
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      consputc(c);  // 同步回显，在屏幕上显示键盘的输入字符，不经过缓冲区，而是直接送到THR寄存器

      // store for consumption by consoleread().
      cons.buf[cons.e++ % INPUT_BUF] = c;

      if(c == '\n' || c == C('D') || cons.e == cons.r+INPUT_BUF){
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();  //初始化串口芯片16550

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
