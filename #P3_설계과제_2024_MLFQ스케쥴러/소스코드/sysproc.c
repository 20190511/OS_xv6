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


int
sys_set_proc_info(void)
{
  int q_level;
  int cpu_burst;
  int cpu_wait_time;
  int io_wait_time;
  int end_time;

  
  if(argint(0, &q_level) < 0)
    return -1;
  if(argint(1, &cpu_burst) < 0)
    return -1;
  if(argint(2, &cpu_wait_time) < 0)
    return -1;
  if(argint(3, &io_wait_time) < 0)
    return -1;
  argint(4, &end_time);


  myproc()->q_level = q_level;
  myproc()->cpu_burst = cpu_burst;
  myproc()->cpu_wait = cpu_wait_time;
  myproc()->io_wait_time = io_wait_time;
  

  if (end_time < 0)
    myproc()->set_time = -1;
  else  
    myproc()->set_time = end_time;


#if !DEBUG
  cprintf("Set Processs %d's info complete\n", myproc()->pid);
#endif
  scheduler_update(myproc());
  return 1;
}