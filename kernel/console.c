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
  if(c == BACKSPACE){  // ��c���˸��
    // if the user typed backspace, overwrite with a space.
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b'); // '\b'�ǽ�������һ��Ȼ���ÿո񸲸�Ҫ�˸���ַ���Ȼ���ٻ��˹��
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;  // ������
  
  // input
#define INPUT_BUF 128
  char buf[INPUT_BUF];  // console�ڲ��Ļ��λ�����
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
} cons;  // ��ʾ����̨����Ϣ��״̬

//
// user write()s to the console go here.
// ͨ��sys_write������ת�������ʱ��user_src��1
int
consolewrite(int user_src, uint64 src, int n)  //��src��ȡn���ַ�д��console��user_srcָʾsrc���û���ַ�����ں˵�ַ
{
  int i;

  acquire(&cons.lock);
  for(i = 0; i < n; i++){
    char c;
    if(either_copyin(&c, user_src, src+i, 1) == -1)  // �������ַsrc�ж�ȡһ���ַ���ŵ�c
      break;
    uartputc(c);  // �Ѷ������ַ�c�����UART�Ļ������������첽��ʾ
  }
  release(&cons.lock);

  return i;
}

//
// user read()s from the console go here.
// copy (up to) a whole input line to dst.
// user_dist indicates whether dst is a user
// or kernel address.
// ������consolewrite������consolereadҲ��ͨ��sys_read������ת������
int
consoleread(int user_dst, uint64 dst, int n)  // ��console��ȡn�ַ���һ�е�dst�У�user_dstָʾdst���û���ַ�����ں˵�ַ
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while(cons.r == cons.w){  // ��������û���ַ������ߵȴ�
      if(myproc()->killed){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF];  // �ӻ�������ȡһ���ַ�

    if(c == C('D')){  // end-of-file  �ж��Ƿ�������Ctrl + D
      if(n < target){  // ����Ѿ���ȡ��һЩ�ַ�
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        cons.r--; // ���˶�ָ�룬��Ctrl+D���浽��һ�ζ�ȡ����
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    cbuf = c;  //������cbuf���c����Ϊcopyout�����ڻ�ı�cbuf��ָ��ĵ�ַ
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)  // ��cbuf��ȡһ���ַ����û��ռ������ַdst
      break;

    dst++;
    --n;

    if(c == '\n'){ //���ж�ȡ�������˳����ζ�ȡ
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;  // ���ر��ζ�ȡʵ�ʶ�ȡ���ַ�����
}

//
// the console input interrupt handler.
// uartintr() calls this for input character.
// do erase/kill processing, append to cons.buf,
// wake up consoleread() if a whole line has arrived.
//
void
consoleintr(int c)   // ���ַ�����console�Ļ�����cons.buf��
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
      consputc(c);  // ͬ�����ԣ�����Ļ����ʾ���̵������ַ���������������������ֱ���͵�THR�Ĵ���

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

  uartinit();  //��ʼ������оƬ16550

  // connect read and write system calls
  // to consoleread and consolewrite.
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
