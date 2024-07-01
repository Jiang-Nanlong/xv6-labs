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
  // ����PLIC����UART�жϺ�VIRTIO�жϣ��������ж�·�ɵ�CPU
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;  // ������Ӧ�жϵ����ȼ���PLIC�涨��0-7��8�����ȼ�������Խ�����ȼ�Խ�ߣ�0��ʾ�����жϡ��˴��������ȼ�����Ϊ1��
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;  // ��������ж��ź�ͬʱ�������ӦID�Ž�С���Ǹ�
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // set uart's enable bit for this hart's S-mode.  s-mode���ǹ�����ģʽ
  *(uint32*)PLIC_SENABLE(hart)= (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);  // �Ե�ǰ��CPU���Ľ�UART0��VIRTIO0�ж�λ��Ϊ1��ʹ�ܶ����������͵��ж���Ӧ

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;  // ����s-mode�µ����ȼ���ֵΪ0��PLIC������ӦС�����ȼ���ֵ���жϡ�Ϊ�������е��ж϶��õ���Ӧ���������ֵ��Ϊ0
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);  // ��ȡ��ǰCPU���ĵ��ж�����Ĵ�����ֵ��Ҳ���ǵ�ǰCPU���Ĵ�������жϣ�PLIC����CPU����ͬ�⣬ֱ���жϷ��������
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
