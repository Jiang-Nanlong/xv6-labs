#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // 设置PLIC接收UART中断和VIRTIO中断，进而将中断路由到CPU
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;  // 设置响应中断的优先级。PLIC规定有0-7共8个优先级，数字越大优先级越高，0表示屏蔽中断。此处两个优先级都设为1。
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;  // 如果两个中断信号同时到达，则响应ID号较小的那个
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // set uart's enable bit for this hart's S-mode.  s-mode就是管理者模式
  *(uint32*)PLIC_SENABLE(hart)= (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);  // 对当前的CPU核心将UART0和VIRTIO0中断位设为1，使能对这两个类型的中断响应

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;  // 设置s-mode下的优先级阈值为0，PLIC不会响应小于优先级阈值的中断。为了让所有的中断都得到相应，这里把阈值设为0
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);  // 获取当前CPU核心的中断认领寄存器的值，也就是当前CPU核心待处理的中断，PLIC不经CPU核心同意，直接中断分配给核心
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
