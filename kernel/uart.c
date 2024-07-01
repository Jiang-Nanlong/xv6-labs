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
char uart_tx_buf[UART_TX_BUF_SIZE];  // UART�Ļ��λ��������������ݴ�Ҫ�첽���Ե���Ļ�ϵ��ַ�
int uart_tx_w; // write next to uart_tx_buf[uart_tx_w++]
int uart_tx_r; // read next from uart_tx_buf[uar_tx_r++]

extern volatile int panicked; // from printf.c

void uartstart();

void
uartinit(void)
{
  // disable interrupts.
  WriteReg(IER, 0x00);  // ��UART��IER�Ĵ�����Ϊ0�����жϡ�IER�Ĵ���������оƬ�����е��жϵ�ʹ��

  // special mode to set baud rate. �������ò����ʵ�����ģʽ
  WriteReg(LCR, LCR_BAUD_LATCH);  // ���ô��ڵĲ����ʣ�LCR�Ĵ�����bit7д��1�󣬻�ı�000��001����λ�ö�Ӧ�ļĴ�����ԭ��000��ӦRHR��THR��һ�����ƶ���һ������д
  //001��ӦIER�Ĵ�����Ҳ�����ϱ߹���uart�жϵļĴ��������ı�LCR�Ĵ���bit7Ϊ1��000��001�ֱ��ӦDLL��DLM�����Ĵ������������Ĵ�������ȷ�������ʡ�

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);  //DLL�Ĵ���д��3

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);  //DLM�Ĵ���д��0

  // leave set-baud mode, �뿪���ò����ʵ�����ģʽ
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS); // ���ô��䴫���ֳ���LCR�Ĵ����ĵ���λΪ00,01,10,11ʱ���ֱ��Ӧ�����ֳ�Ϊ5,6,7,8

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);  //���ò�ʹ��IO

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);  // ʹ����������жϣ�uart������CPU�����ж�

  initlock(&uart_tx_lock, "uart");   // ��ʼ������оƬ16550�����������������ʱ������һ����СΪ32�Ļ��λ�����uart_tx_buf��������UART��Ҫ���͵�����
}

// add a character to the output buffer and tell the
// UART to start sending if it isn't already.
// blocks if the output buffer is full.
// because it may block, it can't be called
// from interrupts; it's only suitable for use
// by write().
void
uartputc(int c)   // ���ַ�c�洢��uart_tx_buf��Ȼ���첽��ʾuart_tx_buf������ݵ���Ļ��
{
  acquire(&uart_tx_lock);

  if(panicked){  // ����ں˷������ϣ�ֱ��������ѭ��������ʧȥ��Ӧ
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
      uart_tx_w = (uart_tx_w + 1) % UART_TX_BUF_SIZE;   // uart_tx_buf���ɻ���
      uartstart();  // �첽��ʾ
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
uartstart()  // ��uart��buf�ж�ȡ���ݣ�Ȼ��д��THR�Ĵ�������ֱ������UARTоƬ�������ݵĺ���
{            // һ�����ε���uartstart������ÿ�ε���ǰ�����uart_tx_buf��������ֹ�������ͬʱ��THR�Ĵ���дֵ
  while(1){
    if(uart_tx_w == uart_tx_r){
      // transmit buffer is empty.
      return;
    }
    
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){  // �����������Ϊ�գ�����UART��û�������һ�η��ͣ���ʱֱ�ӷ���
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }
    
    int c = uart_tx_buf[uart_tx_r];
    uart_tx_r = (uart_tx_r + 1) % UART_TX_BUF_SIZE;
    
    // maybe uartputc() is waiting for space in the buffer.
    wakeup(&uart_tx_r);  // ����uartputc���������򻺳���û�ж���Ŀռ������˯�ߵļ���uart_tx_r�ϵ���
    
    WriteReg(THR, c);  // ����THR�Ĵ������ᱻUART�Զ����з���
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
    int c = uartgetc();  //��ȡ����������ַ����Ӽ���������ַ����ȿ�ʼ�����RHR�Ĵ�����
    if(c == -1)
      break;
    consoleintr(c);  // ���ַ�����console�Ļ�����cons.buf��
  }

  // send buffered characters.
  acquire(&uart_tx_lock);
  uartstart();  // ���������Ļ������uart_tx_buf�д����͵�����
  release(&uart_tx_lock);
}
