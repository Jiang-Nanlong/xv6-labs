#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)  // 确保上一个状态是用户态，也就是确保trap是来着用户模式
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);  // 这里修改stvec寄存器的值，是当前处于内核态中如果产生中断或异常就运行这部分的代码。所以这里主要是为了处理从内核空间发出的中断

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();   // ecall时把pc值存储在sepc寄存器中是由硬件自动实现的
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();  //开中断，因为有些系统调用需要长时间来处理，这时候中断被trap相关硬件默认关闭，导致其他中断被阻塞，
    //影响系统整体性能，所以这里手动打开中断，提高系统性能。

    syscall();
  } else if((which_dev = devintr()) != 0){  //如果trap由设备中断产生
    // ok
  } else {  // 如果中断由异常产生，内核将杀死错误进程
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();  //关中断，为了防止接下来的更新stvec寄存器的时候产生新中断，然后运行到用户空间的trap处理代码，即便是我们现在仍在内核空间

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));  //更新stvec寄存器的值，指向uservec函数，供下一次ecall使用

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  // 保存trapframe中的前五个数据，下一次从用户空间转到内核空间的时候可以使用这些数据
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack  kernel_sp存储的是内核栈的栈顶地址，而p->kstack是内核栈的栈底，而每个栈又占用一个PGSIZE，所以栈顶是p->kstack+PGSIZE
  p->trapframe->kernel_trap = (uint64)usertrap;   //这个地方要在uservec中用到，但是对于用户程序都先是从uservec过来的，但是最开始的时候这个地方是怎么设置的呢？
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode  这里SPP bit位控制了接下来的sret指令是想回到user mode还是supervisor mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);   //接下来sret指令会把sepc寄存器中的值复制到pc寄存器

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);  // 这里接下来跳转到trampoline中的userret
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);  //satp作为第二个参数，被保存在a1寄存器中
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){  //
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();  // 获取中断的信号源

    // 根据中断信号源选择不同的处理程序
    if(irq == UART0_IRQ){  // 串口中断
      uartintr();  // 键盘输入
    } else if(irq == VIRTIO0_IRQ){  // 磁盘中断
      virtio_disk_intr();
    } else if(irq){  // 无法识别的中断源
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

