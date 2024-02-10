#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
/**
 * cpuid == 현재 실행되는 CPU 번호 (0번이면 0, 1번이면1...)
 * @return cpu 테이블에서 현재 cpu index
 * @warning proc.c
*/
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrup disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
/**
 * 현재 실행중인 CPU가 유효한지 판단 후 CPU 구조체 (추상화) 리턴
 * @return cpu* 추상화 구조체
 * @warning proc.c
*/
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
/** 현재 실행중인 프로세스 리턴
 * @return 현재 실행중인 프로세스 구조체 proc* 리턴
 * @warning proc.c
*/
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
/**
 * process 가 새로 할당될 때마다 호출
 * 프로세스 할당 후 멤버변수들을 초기화하는 부분 존재
 * 프로세스의 커널 스택을 할당하고 초기화.
 * @return 프로세스를 하나 할당하고 리턴한 proc* 구조체 (실패시 NULL = 0)
 * @warning proc.c
*/
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  //프로세스 중 UNUSED 프로세스 찾으면 found로 이동
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  //프로세스 커널 Runnable 전 초기 사애는 EMBRYO로 설정
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // 프로세스 커널 스택할당 
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  //커널 스택 프레임만큼 sp 증가..
  sp = p->kstack + KSTACKSIZE;

  //커널 스택에 trapframe 을 둠.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  //#P2 설계과제 ticks 설정 --> 둘 다 초기값이 0이므로 trap.c에서 잘 조정할 것
  p->alarm_timer = 0xFFFFFFFF;
  p->alarmticks = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
/**
 * 초기 프로세스할당 시 사용 (프로세스이름 : initcode)
 * @return 없음
 * @warning xv6 초기실행시 실행되는 구문 --> sh을 돌려주는(init.c) 프로세스로 추측됨
*/
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
/**
 * 현재 프로세스의 메모리 공간을 늘리거나 키움
 * @param n : 양수(키움), 음수(줄임)
 * @return 성공시 0 에러시 -1 
*/
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
/**
 * 프로세스 fork 함수
*/
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  // 부모 mm*  복사
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
/**
 * @warning proc.c
*/
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // 모든 파일 디스크립터 + openfile table을 종료시킴
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op(); //로그 작성시작
  iput(curproc->cwd); //현재프로세스 부모위치의 i-node 이 ref-cnt 를 하나 줄임
  end_op(); //log 작성
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
/**
 * 자식 프로세스가 종료될 때까지 기다림
 * @return 성공시 자식 pid, 에러시 -1
 * @warning proc.c
*/
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock); //lock 획득 (병행성문제)
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    //Zombie 프로세스를 찾는 과정 (자식이 fork되서 죽은 케이스)
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      //좀비 상태인 프로세스가 있으면? 
      // ZOMBIE :  **자식프로세스는 종료되었지만 부모프로세스가 wait하지 않은 상태
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack); //커널 스택 할당해제
        p->kstack = 0;
        freevm(p->pgdir); //물리적 pagetable 할당해제 (프로세스 페이지테이블 --> 메인메모리 할당.)
        //프로세스 초기화 및 UNUSED 설정
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock); //리턴 전 lock 해제`
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    // 아무런 자식 프로세스를 가지지 않은 경우 Or 지금 프로세스가 죽은 경우
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    //모든 자식 프로세스가 실행 중이면 sleeplock
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
/**
 * 
 * xv6 스케쥴러를 정의한 함수. 단순히 proc[NPROC] 를 앞에서부터 순회하며 아직 실행하지 않은 프로세스 선택 (RUNNABLE)
 * @warning cpu는 여러개 존재할 수 있으므로 Lock 획득 acquire ~ release를 잘해줄 것
 *
 * @return 스케쥴러는 리턴하지않음! (계속 for문을 돌며 switch함 : sched() 함수 내부에 switch 존재)
 * @warning Scheduler never returns.  It loops, doing: (스케쥴러는 리턴하지않고 계속 루프를 돔)
 * @warning  - choose a process to run (다음에 실행시킬 프로세스를 선택함)
 * @warning - swtch to start running that process  (다음 프로세스를 전환시켜줌)
 * @warning - eventually that process transfers control (프로세스로 컨트롤을 넘겨줌)
 * @warning - switch() 호출로 스케쥴러로 다시 돌아옴 (via swtch back to the scheduler.)
 * @warning - switch() 는 어셈블리어로 다시 리턴시켜주는 함수
 * @warning #3 과제 프로세스 스케쥴러 손보기 과제
 * 
*/
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock); //스케쥴러를 진입하여 Lock 설정
    //Process Queue를 순회하며 process 가 Runnable 상태가 있으면 해당 process 실행
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      //Context Switch (새롭게 할당받은 process 로 context switch)
      switchuvm(p);
      p->state = RUNNING;

      //switch (A,B) : A-->B로 명령어 위치 분기함 (스케쥴러에서 p프로세스로 전환 (어셈블리어))
      swtch(&(c->scheduler), p->context); //Context Switch (Assembly 어로 switch)
      switchkvm(); //Process 가 실행이 종료됨.

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0; //프로세스가 종료되어돌아오면 c->proc=0 으로 설정 (현재 cpu의 프로세스가 없는 상태로 설정)
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
/**
 * 현 프로세스를 종료하고 스케쥴러로 돌아감
 * @warning sched() 내부에서 switch(A,B) 를 호출하여 A->B 로 분기함 (B는 scheduler 이겠지?)
*/
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  //switch (A,B) : A-->B로 명령어 위치 분기함 (스케쥴러로 돌아감)
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
/**
 * 프로세스 양보 함수
 * @warning 내부에서 sched() 호출함
*/
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
/**
 * 프로세스가 가장 처음 fork 될 때 forkret 로 실행된다
 * @warning forkret 로 실행되면 iinit ~ initlog 를 설정하여 프로세스 초기설정을 담당한다.
*/
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock); //스케쥴러가 lock을 잡고있는 상태이므로 풀고 시작

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0; //초기설정이 종료되었으므로 종료
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
/**
 * 현재 프로세스를 sleep하고 커널모드로 스케쥴러로 돌아감 (sched() 호출)
 * @warning : 프로세스 전체를 아우르는 ptable 락을 푸는 동안 atomic 연산을 지원하기 위해 개별적인 lk을 두르고있음
*/
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
/**
 * 자고있는 프로세스 중 파라미터 chan과 동일한 프로세스를 깨움
 * @param chan : 깨울 프로세스 주소 (void*)
*/
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
/**
 * wakeup 과정을 atomic 연산을 지원하기 위해 lock을 걸어줌
*/
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
/**
 * 프로세스를 종료시킴 (프로세스에 kill 플래그(멤버변수) 를 설정함)
 * @param pid : 죽일 프로세스 ID
*/
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      //프로세스 죽이기 flag를 심어줌
      p->killed = 1;
      // Wake process from sleep if necessary.
      //프로세스 상태가 Sleep 라면 Sched에 올려줌
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
/** 디버깅용?*/
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    // 현재 실행되고있는 프로세스 출력 (embryo, sleep, runable ...)
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
