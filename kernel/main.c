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
    kinit();            // physical page allocator  梳理物理内存，使用空闲链表法组织空闲的内存块，头指针是kmem.freelist
    kvminit();          // create kernel page table  建立内核虚拟地址到物理地址的映射
    kvminithart();      // turn on paging  将内核使用的页表根目录地址写入到 SATP 寄存器
    procinit();         // process table  为每个内核进程分配内核栈，并建立映射，内核中并没有位guard page分配物理块，而在用户空间中却为guard page分配了物理块
    trapinit();         // trap vectors
    trapinithart();     // install kernel trap vector  最开始初始化的时候把stvec寄存器的内容设为kernelvec
    plicinit();         // set up interrupt controller
    plicinithart();     // ask PLIC for device interrupts  当前CPU0设置接收UART0_IRQ和VIRTIO0_IRQ中断
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
    kvminithart();  // turn on paging  把CPU0以外的其他CPU的页表设为内核页表
    trapinithart(); // install kernel trap vector  最开始初始化的时候把stvec寄存器的内容设为kernelvec
    plicinithart(); // ask PLIC for device interrupts  除了CPU0以外的其他CPU设置接收UART0_IRQ和VIRTIO0_IRQ中断
  }

  scheduler();
}
