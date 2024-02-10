#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "date.h"

#define P3_TIMER
// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

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
/**
 * 트랩명령어를 처리하기 위한 trap 핸들러함수 (시스템콜 포함)
 * @param tf : 트랩테이블 (보통 인자로 넘겨줄 때 proc->tf)
 * @return 없음
 * @warning tf->trapno 는 trap 함수에 어떻게 왔는지 알려주는 flag와 같음
 * @warning trap.c
*/
void
trap(struct trapframe *tf)
{
  //트랩 번호가 시스템 콜일 경우 내부에서 syscall() 호출
  if(tf->trapno == T_SYSCALL){
    //프로세스가 죽어있으면 프로세스 종료
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall(); //syscall 호출
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER: //Timer Interrupt 의 경우
  //첫 번째 cpu 만 Timer_Tcks 를 늘려주고있음
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++; //ticks 증가! (tcks 호출마다 증가)
      //#P2 alarmticks 설정 시 증가
      if (myproc() && myproc()->alarm_timer != 0xFFFFFFFF)
        myproc()->alarmticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    //두 번째 CPU도 늘려주고싶다면 설정할 것
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
  //무슨 인터럽트인지 모르겠음..
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
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  //주의! xv6는 TIME Ticks 가 1개씩 증가할 때 마다 스케쥴러가 교체되는 RR 형태의 스케쥴러를 채용 중!
  // RR ticks 주기를 늘리고 싶다면 해당 함수를 손 볼것
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  //프로세스 alarm_timer 설정이 되었다면 timer() check
  if (myproc() && myproc()->alarm_timer != 0xFFFFFFFF) {
    if (myproc()->alarmticks >= myproc()->alarm_timer) { //timer 초과여부 확인
      cprintf("SSU_Alarm!\n");
#ifdef P3_TIMER
      //과제 명세에 종료 전 출력부분이 있어 수정
      struct rtcdate r;
      cmostime(&r);
      cprintf("Current time : %d-%d-%d %d:%d:%d\n", r.year, r.month, r.day, r.hour, r.minute, r.second);
#endif
      exit(); //프로세스 종료
    }
  }
}
