//
// low-level driver routines for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.
#define Reg(reg) ((volatile unsigned char *)(UART0 + reg))

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_TX_ENABLE (1<<0)
#define IER_RX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// the transmit output buffer.
struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];  // UART的环形缓冲区，仅用来暂存要异步回显到屏幕上的字符
int uart_tx_w; // write next to uart_tx_buf[uart_tx_w++]
int uart_tx_r; // read next from uart_tx_buf[uar_tx_r++]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);  // 把UART的IER寄存器设为0，关中断。IER寄存器控制着芯片上所有的中断的使能

  // special mode to set baud rate. 进入设置波特率的特殊模式
  WriteReg(LCR, LCR_BAUD_LATCH);  // 设置串口的波特率，LCR寄存器的bit7写入1后，会改变000和001两个位置对应的寄存器，原本000对应RHR和THR，一个控制读，一个控制写
  //001对应IER寄存器，也就是上边管理uart中断的寄存器。当改变LCR寄存器bit7为1后，000和001分别对应DLL和DLM两个寄存器，这两个寄存器用来确定波特率。

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);  //DLL寄存器写入3

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);  //DLM寄存器写入0

  // leave set-baud mode, 离开设置波特率的特殊模式
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS); // 设置传输传输字长，LCR寄存器的低两位为00,01,10,11时，分别对应传输字长为5,6,7,8

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);  //重置并使能IO

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);  // 使能输入输出中断，uart可以向CPU发起中断

  initlock(&uart_tx_lock, "uart");   // 初始化串口芯片16550输出缓冲区的锁，此时设置了一个大小为32的环形缓冲区uart_tx_buf用来保存UART将要发送的数据
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)   // 把字符c存储到uart_tx_buf，然后异步显示uart_tx_buf里的内容到屏幕上
{
  acquire(&uart_tx_lock);

  if(panicked){  // 如果内核发生故障，直接陷入死循环，程序失去响应
    for(;;)
      ;
  }

  while(1){
    if(((uart_tx_w + 1) % UART_TX_BUF_SIZE) == uart_tx_r){
      // buffer is full.
      // wait for uartstart() to open up space in the buffer.
      sleep(&uart_tx_r, &uart_tx_lock);
    } else {
      uart_tx_buf[uart_tx_w] = c;
      uart_tx_w = (uart_tx_w + 1) % UART_TX_BUF_SIZE;   // uart_tx_buf构成环形
      uartstart();  // 异步显示
      release(&uart_tx_lock);
      return;
    }
  }
}

// alternate version of uartputc() that doesn't 
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

// if the UART is idle, and a character is waiting
// in the transmit buffer, send it.
// caller must hold uart_tx_lock.
// called from both the top- and bottom-half.
void
uartstart()  // 从uart的buf中读取数据，然后写到THR寄存器，是直接驱动UART芯片发送数据的函数
{            // 一共两次调用uartstart函数，每次调用前都会给uart_tx_buf上锁，防止多个进程同时向THR寄存器写值
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){  // 如果缓冲区不为空，但是UART并没有完成上一次发送，这时直接返回
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r];
    uart_tx_r = (uart_tx_r + 1) % UART_TX_BUF_SIZE;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);  // 唤醒uartputc函数内正因缓冲区没有多余的空间而导致睡眠的加在uart_tx_r上的锁
    
    WriteReg(THR, c);  // 存入THR寄存器，会被UART自动串行发送
  }
}

// read one input character from the UART.
// return -1 if none is waiting.
int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// handle a uart interrupt, raised because input has
// arrived, or the uart is ready for more output, or
// both. called from trap.c.
void
uartintr(void)
{
  // read and process incoming characters.
  while(1){
    int c = uartgetc();  //获取键盘输入的字符，从键盘输入的字符最先开始存放在RHR寄存器中
    if(c == -1)
      break;
    consoleintr(c);  // 把字符放入console的缓冲区cons.buf中
  }

  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();  // 最后再向屏幕发送在uart_tx_buf中待发送的数据
  release(&uart_tx_lock);
}
