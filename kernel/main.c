#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void main()
{
  if (cpuid() == 0)
  {
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();            // physical page allocator  ���������ڴ棬ʹ�ÿ���������֯���е��ڴ�飬ͷָ����kmem.freelist
    kvminit();          // create kernel page table  �����ں������ַ�������ַ��ӳ��
    kvminithart();      // turn on paging  ���ں�ʹ�õ�ҳ���Ŀ¼��ַд�뵽 SATP �Ĵ���
    procinit();         // process table  Ϊÿ���ں˽��̷����ں�ջ��������ӳ�䣬�ں��в�û��λguard page��������飬�����û��ռ���ȴΪguard page�����������
    trapinit();         // trap vectors
    trapinithart();     // install kernel trap vector  �ʼ��ʼ����ʱ���stvec�Ĵ�����������Ϊkernelvec
    plicinit();         // set up interrupt controller
    plicinithart();     // ask PLIC for device interrupts  ��ǰCPU0���ý���UART0_IRQ��VIRTIO0_IRQ�ж�
    binit();            // buffer cache
    iinit();            // inode cache
    fileinit();         // file table
    virtio_disk_init(); // emulated hard disk
    userinit();         // first user process
    __sync_synchronize();
    started = 1;
  }
  else
  {
    while (started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();  // turn on paging  ��CPU0���������CPU��ҳ����Ϊ�ں�ҳ��
    trapinithart(); // install kernel trap vector  �ʼ��ʼ����ʱ���stvec�Ĵ�����������Ϊkernelvec
    plicinithart(); // ask PLIC for device interrupts  ����CPU0���������CPU���ý���UART0_IRQ��VIRTIO0_IRQ�ж�
  }

  scheduler();
}
