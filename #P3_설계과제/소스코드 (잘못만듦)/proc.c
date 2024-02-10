#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef true
#define true    1
#define false   0
#endif

#define MAX_IDX    25
#define SUB_IDX     4

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;
typedef struct{
    struct proc* head;
    struct proc* tail;
    int queueCnt;
}procQ;

typedef struct {
    procQ queue[4];
    int middleCnt;
}Priority;

Priority RunQueue[MAX_IDX] = {0};
int nextpid = 1;


extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

//추가. -> 배준형(20190511)
/*******************************/
int  getSmallestPri()
{  
    int i,j;
    for (i = 0 ; i < MAX_IDX ; i++) {
        if (!RunQueue[i].middleCnt)
            continue;
        for (j = 0 ; j < SUB_IDX ; j++) {
            if(!RunQueue[i].queue[j].queueCnt)
                continue;
            return i*4+j;
        }
    }
    return 0;
}

struct proc* deleteQueue(procQ* queue, struct proc* retProc)
{
    if (queue == NULL || retProc == NULL)
        return NULL;
    
    if (queue->queueCnt == 1)
        queue->head = queue->tail = NULL;
    else if (retProc == queue->head)
    {
        retProc->next->prev = NULL;
        queue->head = queue->head->next;
    }
    else if (retProc == queue->tail)
    {
        retProc->prev->next = NULL;
        queue->tail = queue->tail->prev;
    }
    else
    {
        retProc->prev->next = retProc->next;
        retProc->next->prev = retProc->prev;
    }
    retProc->next = retProc->prev = NULL;
    queue->queueCnt--;
    return retProc;
}

struct proc* getHighPri()
{
    int i, j;
    struct proc* retProc;
    procQ* queue;
    for (i = 0 ; i < MAX_IDX ; i++) {
        if(!RunQueue[i].middleCnt) 
            continue;
        for (j = 0 ; j < SUB_IDX ; j++) {
            queue = &(RunQueue[i].queue[j]);
            if (!queue->queueCnt)
                continue;
            for (retProc = queue->head ; retProc && retProc->state != RUNNABLE ; retProc = retProc->next);
            if (!retProc)
                continue;
            else if (retProc->state == RUNNABLE) {
                retProc = deleteQueue(queue, retProc);
                if (retProc == NULL)
                    return NULL;
                RunQueue[i].middleCnt--;
                return retProc;
            }
        }
    }
    return NULL;
}

void appendProc(struct proc* proc)
{
    if (proc == NULL)
        return;
    procQ* queue = &(RunQueue[proc->priority/4].queue[proc->priority % 4]);
    if (queue->queueCnt == 0) {
        proc->next = proc->prev = NULL;
        queue->head = queue->tail = proc;
        queue->queueCnt++;
    }
    else {
        proc->prev = queue->tail;
        proc->next = NULL;
        queue->tail->next = proc;
        queue->tail = proc;
        queue->queueCnt++;
    }
    RunQueue[proc->priority/4].middleCnt++;
}

void initQueue()
{
    int i, j;
    for (i = 0 ; i < MAX_IDX ; i++) {
        RunQueue[i].middleCnt = 0;
        for (j = 0 ; j < SUB_IDX ; j++) {
            RunQueue[i].queue[j].head = RunQueue[i].queue[j].tail = NULL;
            RunQueue[i].queue[j].queueCnt = 0;
        }
    }
}

void updateQueue()
{
    int i, j;
    procQ* queue;
    struct proc* tmp, *updateNode;
    for (i = 0 ; i < MAX_IDX ; i++) {
        if (!RunQueue[i].middleCnt)
            continue;
        for (j = 0 ; j < SUB_IDX ; j++)  { 
            queue = &(RunQueue[i].queue[j]);
            if (!queue->queueCnt)
                continue;
            for (tmp = queue->head ; tmp != NULL ;) {
                if (!tmp->proc_tick) {
                    tmp = tmp->next;
                    continue;
                }
                updateNode = tmp;
                tmp = tmp->next;
                updateNode = deleteQueue(queue, updateNode);
                RunQueue[i].middleCnt--;
                if (updateNode != NULL) {
                    updateNode->priority = updateNode->priority + updateNode->proc_tick/10;
                    updateNode->priority = updateNode->priority > 99 ? 99 : updateNode->priority;
                    updateNode->proc_tick = 0;
                    appendProc(updateNode);
                }
            }
        }
    }
}

/********************************************************************/
void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
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
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  // lock 살리는 방향으로.... queue는 갱신할 필요는 없는듯.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
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

  
  p->next = p->prev = NULL;
  p->priority = getSmallestPri();
  p->proc_tick = p->cpu_used = p->sched_timer = 0;
  p->proc_deadline = -1;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
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
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  p->priority = p->priority + p->proc_tick/10;
  p->priority = p->priority > 99 ? 99 : p->priority;
  appendProc(p);
  cprintf("userinit start %d\n", RunQueue->middleCnt);
  p->state = RUNNABLE;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
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
  np->priority = np->priority + np->proc_tick/10;
  np->priority = np->priority > 99 ? 99 : np->priority;
  appendProc(np);
  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
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
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  uint counter = 0;
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    
    acquire(&ptable.lock);

    if (!(p = getHighPri())) {
      release(&ptable.lock);
      continue;
    }

    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
      
    #ifdef DEBUG
    cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d\n",
       p->pid, p->priority, p->proc_tick, p->cpu_used);
    #endif
    swtch(&(c->scheduler), p->context);
    switchkvm();
    c->proc = 0;
    counter += p->proc_tick;
    if (counter > 60) {
      counter = 0;
      updateQueue();
    }
    release(&ptable.lock);
    /*
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    */

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
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
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
//CPU 양보 -> scheduler 우선선위 재갱신
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock

  /*
  myproc()->priority = myproc()->priority + myproc()->proc_tick/10;
  myproc()->priority = myproc()->priority > 99 ? 99 : myproc()->priority;
  */
  appendProc(myproc());   //우선순위 재갱신 후 다시 집어넣음
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
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

// P3 wakeup -> pri 최솟값..
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      p->priority = getSmallestPri(); //가장 작은 우선순위를 갖도록 설정
      /*
      p->priority = p->priority + p->proc_tick/10;
      p->priority = p->priority > 99 ? 99 : p->priority;
      */
      appendProc(p);
    }
}

// Wake up all processes sleeping on chan.
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
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
        /*
        p->priority = p->priority + p->proc_tick/10;
        p->priority = p->priority > 99 ? 99 : p->priority;
        */
        appendProc(p);
        p->state = RUNNABLE;
      }
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
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}



