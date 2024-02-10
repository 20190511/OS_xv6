#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 프로세스 우선순위와 tick 설정 (바로 설정이 아닌 재갱신 때 반영될 예정)
int 
sys_set_sche_info(void)
{
  int priority, tcks;
  argint(0, &priority); //0번 argument 가져옴
  argint(1, &tcks); //1번 정수형 arguement 가져움

  if (priority < 0)
    priority = 0;
  else if (priority > 99)
    priority = 99;

  //프로세스 tick 설정 
  if (tcks < 0) { //ticks 음수 처리 -> 실패로 간주
    return -1;
  } 
  cprintf("set_sche_info() pid = %d\n", myproc()->pid);
#ifdef ANALY
  cprintf("[SET] pid : %d, priority : %d, schedule_ticks : %d\n", myproc()->pid, priority, tcks);
#endif
  myproc()->proc_deadline = tcks;  //timer ticks 설정
  myproc()->priority = priority; //우선순위 설정

  /** cpu값을 바로 적용하면..? 11.12 */
  // 예시 결과와도 차이가 나고, 이전 방식이 더 안정적이라 일단 폐기
  /*
  pushcli(); //Interrupt 활성화
  mycpu()->scheduler_flag = 1; //현 cpu에게 update flage 설정
  popcli(); //interrupt 끄기
  yield(); //update를 위한 yield();
  */
  return -1;
}

//현재 ticks를 알려주는 단순한 코드 
int
sys_myticks(void)
{
  int tck;
  acquire(&tickslock); //병행성 처리를 위해 둘러싼 ticks
  tck = ticks;
  release(&tickslock);
  return tck;
}
