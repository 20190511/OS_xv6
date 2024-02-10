#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;
uint scheduler_tick; //스케쥴러 재갱신 전용 tick
int proc_tick_lock = 0; //스케쥴러 재갱신 tick을 증가시키기위한lock (pid 3이상부터 측정 -> 제일처음 실행되는 프로그램부터..)

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  //시스템 콜일 때 trap
  if(tf->trapno == T_SYSCALL){
    //중간에 프로세스 죽었으면 종료시킴
    if(myproc()->killed) {
      exit();
    }
    //현 프로세스의 trapframe을 갱신시켜줌 -> eax로 syscall 찾기가능
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed){
      exit();
    }
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      // 스케쥴링 시간동안 cpu_used를 증가.
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER){
    exit();
  }

  //프로세스 Timer Interrupt인 경우에만 진입
  if (myproc() && tf->trapno == T_IRQ0 + IRQ_TIMER) {
    if (myproc()->pid >= 3 && proc_tick_lock == 0) {
      //만약에 pid가 3이상 (userprogram 시작) 인데 lock이 안 풀려있으면 lock을 풀어줌
      proc_tick_lock = 1;
    }
    myproc()->cpu_used++;
    myproc()->priority_tick++;
    myproc()->proc_tick++;
    //process pid 가 3이상일 때 스케쥴링 시간을 측정함
    if (myproc()->pid != 1 && myproc()->pid != 2 && proc_tick_lock)
      scheduler_tick++;

    //cprintf("my cpu ticks : %d\n", mycpu()->proc_tick);
  }
  
  //P3 -> TimeQuantum 60s 로 변경
  if(myproc() && myproc()->state == RUNNING && tf->trapno == T_IRQ0+IRQ_TIMER) {
      //CPU 사용예약시간 초과 시 종료처리

      //Process Deadline 넘어가면 종료시키기 
      if (myproc() && myproc()->proc_deadline != -1 && myproc()->proc_deadline <= myproc()->cpu_used) {

    #ifdef DEBUGS
        cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks (3)\n",
              myproc()->pid, myproc()->priority, myproc()->proc_tick, myproc()->cpu_used);
    #endif
    #ifdef ANALY
        acquire(&tickslock);
        cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks, totalTicks : %d (3)\n",
                  myproc()->pid, myproc()->priority, myproc()->proc_tick, myproc()->cpu_used, ticks);
        release(&tickslock);
    #endif
        cprintf("PID : %d terminated\n", myproc()->pid);
        exit();
      }

      //cpu 시간이 30 지나면 yield(); -> Scehduler진입
      if (myproc() && myproc()->proc_tick >= 30) {
    #ifdef DEBUGS
    cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks (1)\n",
       myproc()->pid, myproc()->priority, myproc()->proc_tick, myproc()->cpu_used);
    #endif
        myproc()->proc_tick = 0;
        yield();
      }

      //scheduler_tick 프로세스 tick을 측정하고 있다가 60이 넘어가면 cpu에 재갱신 플래그를 만들고 진입
      if (scheduler_tick >= 60) {
        mycpu()->scheduler_flag = 1;
        scheduler_tick = 0;
        yield(); //priority 재갱신하러 진입
      }
     }

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
