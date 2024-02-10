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

#define JHS  0
#define MAX_IDX    25

// ifdef NEWS  -> 1 Queue  in 1 RunQueue
// else -> 4 Queues in 1 RunQueue (기본형)
#if     ORIGIN
#else
#define SUB_IDX     4
#endif

static struct proc *initproc;

#ifndef NEWS
//Priroity 내부에 4개의 우선순위를 모두 갖는 1개의 Queue
typedef struct {
    struct proc* head;
    struct proc* tail;
    int queueCount;
}Priority;
#else
//Process 내부에는 각 Priority에 해당하는 procQ가 4개씩 존재함
typedef struct{
    struct proc* head;
    struct proc* tail;
    int queueCnt;
}procQ;

typedef struct {
    procQ queue[4];
    int middleCnt;
}Priority;
#endif

//병행성 처리를 위한 락
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

int nextpid = 1;


extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

Priority RunQueue[MAX_IDX]; //RunQueue
//추가. -> 배준형(20190511)
// default 경우 -> 1Queues in 1 RunQueue
// NEWS=true  -> 4Queues in 1 RunQueue
/*******************************/

#ifndef NEWS
struct proc* deleteQueue(Priority* queue, struct proc* ptr)
{
  if (ptr == queue->head && ptr == queue->tail)
  { // p 뿐일 때
    queue->head = queue->tail = NULL;
  }
  else if (ptr == queue->head)
  { // p가 시작부분일 때
    queue->head = ptr->next;
    ptr->next->prev = NULL;
  }
  else if (ptr == queue->tail)
  { // p가 마지막 부분일 때
    queue->tail = ptr->prev;
    ptr->prev->next = NULL;
  }
  else
  { // p가 중간부분일 때 중간부분 연결
    ptr->prev->next = ptr->next;
    ptr->next->prev = ptr->prev;
  }
  queue->queueCount--;
  return ptr;
}
struct proc* getHighPri()
{
    int idx = 0;
    Priority *queue;
    struct proc* retProc, *temp;
    //Queue를 순회하며 가장 우선순위 작은 프로세스 찾기
    for (idx = 0 ; idx < MAX_IDX; idx++) {
        if (RunQueue[idx].head == NULL)
            continue;
        queue = &RunQueue[idx]; //포인터를 하기쉽게 queue를 간이적으로 할당
        struct proc* ptr = queue->head;  //Queue 이동을 위한 pointer
        for (; ptr != NULL; ptr = ptr->next)  {
          if (ptr->state == RUNNABLE) //만약 상태가 RUNNABLE 이면 가장 작은 우선순위 찾은것임
            break;
        }
        //뽑은 큐가 RUNNABLE 상태가 아니고 tail이라서 뽑힌경우 
        if (ptr == NULL || ptr->state != RUNNABLE) 
            continue;
        
        // 11.11 조현웅 학우분 반영 추가 : 우선순위가 동일할 시 CPU_USED 가 적은 순서가 우선이다.
        temp = ptr;
        if (temp->pid != 1 && temp->pid != 2) { //1,2번 프로세스는 일단 무조건 뽑힌것으로 간주
          while (temp->next != NULL && temp->priority <= ptr->priority) { 
            temp = temp->next;
            //우선순위가 동일한데 cpu 이용률이 더 적으면 ptr재갱신
            if (temp->cpu_used < ptr->cpu_used && temp->priority <= ptr->priority) {
              ptr = temp;
            }
          }
        }
        retProc = ptr; 
        deleteQueue(queue, retProc); //뽑힌 프로세스는 RunQueue에서 삭제
        return retProc;
    }
    return NULL;
}

/**
 * process 를 우선순위 고려해서 삽입
*/
void appendProc(struct proc* process)
{

    Priority* queue = &RunQueue[process->priority/4]; //RunQueue에서 해당 프로세스 위치찾기

    //RunQueue가 비어있는 첫 번째 노드라면?
    if(queue->head == NULL && queue->tail == NULL) {
      //첫 번째 노드로 갱신
        process->next = process->prev = NULL;
        queue->head = queue->tail = process;
        queue->queueCount++;
        return;
    }
    else { //RunQueue가 첫 번째 노드가 아님
        struct proc* ptr;
        for (ptr = queue->tail ; ptr->priority > process->priority && ptr != queue->head ; ptr = ptr->prev) {
        }

        //ptr보다 앞에 삽입되어야하만다면?
        if (ptr->priority > process->priority) {
            if (ptr == queue->head) { 
              //심지어 head위치에 삽입되어야하면? head위치로 삽입
                process->prev = NULL;
                process->next = ptr;
                queue->head = process;
                ptr->prev = process;
            }
            else {
                //이외에는 ptr 앞에 process 끼어넣기
                process->prev = ptr->prev;
                process->next = ptr;
                ptr->prev->next = process;
                ptr->prev = process;
            }
        }
        else {
            //ptr보다 뒤에 삽입되어야 한다면?
            if (ptr == queue->tail) {
                //ptr보다 뒤에 있어야되는데 ptr이 tail이면 ptr 재갱신
                process->next = NULL;
                process->prev = ptr;
                queue->tail = process;
                ptr->next = process; 
            }
            else {
              //이외에는 ptr뒤에 process 끼어넣기
                process->prev = ptr;
                process->next = ptr->next;
                ptr->next->prev = process;
                ptr->next = process;
            }

        }
        queue->queueCount++;
        return;
    }        
    
}


void updateQueue()
{
  //ptable.proc[0].priority = ptable.proc[1].priority = 99;
  //ptable.proc[0].proc_tick = ptable.proc[1].proc_tick = 0;
    int i;
    struct proc *p, *ptr = NULL, *tail = NULL;
    Priority* queue;

    //RunQueue 순회
    for (i = 0 ; i < MAX_IDX ; i++) { 
        queue = &RunQueue[i];
        //우선순위 갱신이 필요하면 Queue에서 빼서 queue 리스트로 연결
        for (p = queue->head ; p != NULL ;) {
            ptr = p;
            p = ptr->next; //다음 노드로 이동
            if (ptr->pid == 1 || ptr->pid == 2) { //하필이면 뽑힌 프로세스가 pid: 1,2면 패스
              ptr->priority_tick = 0;
              continue;
            }
            //검사해보니까 priority_tick도 사용해있거나 우선순위가 맞지않으면 재갱신 시도
            if (ptr->priority_tick != 0 || ptr->priority/4 != i) {
                deleteQueue(queue, ptr);
                //우선순위 재갱신

                //우선순위 갱신은 바로바로 한는게 아니라 한꺼번에 간이 리스트로 연결해뒀다가 한 번에 연결
                ptr->prev = ptr->next = NULL;
                if (tail == NULL) {
                  tail = ptr;
                }
                else {
                  ptr->prev = tail;
                  tail = ptr;
                }
            }
        }        
    }

    //tail에 연결된 연결리스트 순회하며 출력
    while (tail != NULL)
    {

#if JHS
      struct proc* tmp = tail;
      for (; tmp != NULL ; tmp = tmp->prev)  {
        cprintf("%d(%d)->", tmp->pid, tmp->priority_tick);
      }
      cprintf("\n");
#endif
  
      //tail 위치를 다음꺼로 연결하기 위해 tail을 미리작업
      ptr = tail;
      tail = tail->prev;
      ptr->prev = ptr->next = NULL;

      //우선순위 priority += priority_ticks / 10 으로 재갱신
      ptr->priority = ptr->priority + ptr->priority_tick / 10;
      ptr->priority = ptr->priority > 99 ? 99 : ptr->priority;
      ptr->priority_tick = 0;
      // 재삽입
      appendProc(ptr);
    }
}

/**
 * 가장 작은 우선순위를 찾아내는 함수 없을 시 0 리턴
*/
int  getSmallestPri()
{
    int idx = 0;
    struct proc* p;

    //Queue 순회를하며 가장 작은 우선순위 pickup 
    for (; idx < MAX_IDX ; idx++) {
      p = RunQueue[idx].head;
      while (p != NULL) {
        //찾은 프로세스가 RUNNABLE이면 최소의 priority라 간주하고 리턴
        if (p->state == RUNNABLE) {
          
          while (p != NULL && (p->pid == 1 || p->pid == 2)) {
            p = p->next;
          }
          if (p != NULL && p->state == RUNNABLE)
            return p->priority;
          else if (p == NULL)
            break;
          else
            p = p->next;
          //cprintf("[%d] -> priority : %d\n", p->pid, p->priority);  
        }
        else  {
          p = p->next;
        }
      }
    }
    return 0;
}
#else


int  getSmallestPri()
{  
    int i,j;
    struct proc* p; 
    for (i = 0 ; i < MAX_IDX ; i++) {
        if (!RunQueue[i].middleCnt)
            continue;
        //4개의 Queue 중 서브 Queue 순회
        for (j = 0 ; j < SUB_IDX ; j++) {
            //SubQueue가 개수가 0개 이상인경우 확인
            if (!RunQueue[i].queue[j].queueCnt) {
                continue;
            }
            p = RunQueue[i].queue[j].head;
            while (p != NULL) {
              //SubQueue의 번호를 확인
              if (p->state == RUNNABLE) {
                while (p != NULL && (p->pid == 1 || p->pid == 2))
                  p = p->next;

                if (p != NULL && p->state == RUNNABLE) {
                  return i * 4 + j;
                }
                else if (p == NULL) 
                  break; 
                else
                  p = p->next;
              }
              else
                p = p->next;
            }
            //cprintf("[%d] state : %d\n", RunQueue[i].queue[j].head->pid, RunQueue[i].queue[j].head->state);
        }
    }
    return 0;
}

struct proc* deleteQueue(procQ* queue, struct proc* retProc)
{
  //사실상 위의 deleteQueue와 매커니즘 동일함
    if (queue == NULL || retProc == NULL)
        return NULL;

    //Queue요소개수가 1개면 head,tail에 연결 
    if (queue->head == queue->tail) {
        queue->head = queue->tail = NULL;
        queue->queueCnt = 0;
    }
    else if (retProc == queue->head) //Queue의 맨 앞을 삭제하는 경우
    {
        retProc->next->prev = NULL; 
        queue->head = queue->head->next;
    }
    else if (retProc == queue->tail) //Queue의 맨 뒤를 삭제하는 경우
    {
        retProc->prev->next = NULL;
        queue->tail = queue->tail->prev;
    }
    else //중간 노드를 삭제하는 경우
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
    struct proc* retProc, *temp;
    procQ* queue;
    // Queue를 처음부터 순회
    for (i = 0 ; i < MAX_IDX ; i++) {
      //Queue의 중간 Queue들의 각 개수를 확인하는 부분
        if(!RunQueue[i].middleCnt) 
            continue;
        //SubQueue를 차례대로 순회하며 process 선택 준비
        for (j = 0 ; j < SUB_IDX ; j++) {
            queue = &(RunQueue[i].queue[j]);
            if (!queue->queueCnt)
                continue;
              // Sub Queue에서 RUNNING인 prcess를 찾을때까지 iteration 진행
            for (retProc = queue->head ; retProc && retProc->state != RUNNABLE ; retProc = retProc->next);
            if (!retProc)
                continue;
              // retProc가 RUNNABLE 상태인 경우
            else if (retProc->state == RUNNABLE) {
              // 11.11 조현웅 학우분 반영 추가 : 우선순위가 동일할 시 CPU_USED 가 적은 순서가 우선이다.
              temp = retProc;
              //1,2번 PID는 선택되면 무조건 리턴 (idle는 초반에 실행되고 실행이 되지않지만, 초반엔 무조건 실행되어야함)
              if (temp->pid != 1 && temp->pid != 2) {
                while (temp->next != NULL && temp->priority <= retProc->priority) {
                  temp = temp->next;
                  //우선순위가 동일하거나 (작으면서) CPU사용시간이 적으면 해당 노드로 retProc 갱신
                  if (temp->cpu_used < retProc->cpu_used && temp->priority <= retProc->priority) {
                    retProc = temp;
                  }
                }
              }
              //cprintf("%d state %d\n", retProc->pid, retProc->state);
              //다음에 스케쥴될 Process는 RunQueue에서 삭제
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

/**
 * RunQueue에 proc 를 추가하는 함수
*/
void appendProc(struct proc* proc)
{
    if (proc == NULL)
        return;
      //삽입될 위치 확인
    procQ* queue = &(RunQueue[proc->priority/4].queue[proc->priority % 4]);
    if (queue->head == NULL && queue->tail == NULL) {
      //만약 RunQueue가 비어있다면 초기세팅구성
        proc->next = proc->prev = NULL;
        queue->head = queue->tail = proc;
        queue->queueCnt = 1;
    }
    else {
      //RunQueue가 비어있지않다면 tail 위치에다가 Proecss 삽입
        proc->prev = queue->tail;
        proc->next = NULL;
        queue->tail->next = proc;
        queue->tail = proc;
        queue->queueCnt++;
    }
    RunQueue[proc->priority/4].middleCnt++;
}

//프로세스 초기화부분을 세팅하는 함수
//원래 배치될 부분은 scheduler()실행 전인 userinit()에 넣는게 맞음
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

/**
 * Priority 재갱신이 필요할 때 호출하는 함수
 * 스케쥴러 내부에서 호출될 예정
*/
void updateQueue()
{
    int i, j;
    procQ* queue;
    struct proc* tmp, *updateNode;
    //RunQueue Index를 차례차례 순회
    for (i = 0 ; i < MAX_IDX ; i++) {
      //RunQueue내부의 4개의 큐가 모두 비어있음을 확인하는 middleCnt 확인
        if (!RunQueue[i].middleCnt)
            continue;
        //RunQueue 내부 Queue하나하나에 접근
        for (j = 0 ; j < SUB_IDX ; j++)  { 
            queue = &(RunQueue[i].queue[j]);
            if (!queue->queueCnt)
                continue;
            //RunQueue를 순회하며 업데이트해야될 process를 찾는과정
            for (tmp = queue->head ; tmp != NULL ;) {
              //prioriy_tick도 0 tick이 아니면서 우선순위도 맞지 않는 경우를 찾음
                if (tmp->priority_tick == 0 && tmp->priority/4 == i) {
                    tmp = tmp->next;
                    continue;
                }
                //update 할 노드를 찾고 해당 노드를 재삽입을 위한 삭제를함
                //그냥 삭제+삽입 할 경우 연결리스트의  구조가 깨지기 때문에 tmpNode 따로 두기
                updateNode = tmp;
                tmp = tmp->next;
                //update를 위해 해당 노드를 Queue에서 빼냄
                updateNode = deleteQueue(queue, updateNode);
                RunQueue[i].middleCnt--;
                //update진행
                if (updateNode != NULL) {
                  //우선순위 재계산 후 업데이트 진행
                    updateNode->priority = updateNode->priority + updateNode->priority_tick/10;
                    updateNode->priority = updateNode->priority > 99 ? 99 : updateNode->priority;
                    updateNode->priority_tick = 0;
                    appendProc(updateNode);
                }
            }
        }
    }
}
#endif

/********************************************************************/


//pTable락을 초기화하는 부분
void
pinit(void)
{
  //cprintf("pinit : %d\n", myproc()->pid);
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
  pushcli(); //인터럽트 불능화
  c = mycpu();
  p = c->proc;
  popcli(); //인터럽트 불능화 해제
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

  //초기 락을 설정
  acquire(&ptable.lock);

  //ptable을 단순순회하며 UNUSED 프로세스가 있으면 found로 이동
  //goto를 쓴 이유는 모르곘음 (안정성 떨어져보임)
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:

//아직 프로세스가 할당받기전이라 EMBRYO 상태
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack. 프로세스 커널스택을 하나 할당받음
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE; //프로세스 kernel 스택 위로 이동

  // Leave room for trap frame.
  //kernel stack 맨 윗부분에는 trapframe을 설정
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret; //1번 프로세스처럼 가장 처음에 실행되는 프로세스는 iinit(), initlog() 설정을 위한 이동

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

#if ANALY
  acquire(&tickslock);
  cprintf("PID : %d, %d (0)\n", p->pid, ticks);
  release(&tickslock);
#endif
  //P3 과제를 위한 프로세스 설정
  p->proc_tick = 0; //생성된 시점에서 proc_tick=0으로 설정
  p->priority_tick = p->cpu_used = 0;
  p->proc_deadline = -1;
  p->priority = getSmallestPri();
  //프로세스 우선순위는 0,1,2 ilde를 제외한 가장 작은 값으로 설정 
  if (p->pid == 0 || p->pid == 1 || p->pid == 2)
    p->priority = 99;
  /*
  else if (p->priority == 99) //만약 99로 설정된다면 0으로 설정
    p->priority = 0;
  */
  appendProc(p);
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  //cprintf("userinit : %d\n", myproc()->pid);
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
  appendProc(p);
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
  if((np = allocproc()) == 0){ //프로세스 생성
    return -1;
  }

  // Copy process state from proc.
  //페이지 디렉토리는 부모 디렉토리를 복사
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  //부모프로세스 trapframe 등을 다 복사
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  //파일 디스크립터 부모꺼 복사 
    // 이부분을 수정하면 예전에 리시프에서 GETFD, SETFD 로 CLOSE_ON_EXEC 부분을 조절가능할 듯
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  //부모 이름을 복사 (디버그시 사용)
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid; //자식 pid 리턴

  acquire(&ptable.lock);

  //appendProc(np);
  np->state = RUNNABLE;

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
//#define DEBUG
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
#if NEWS
  cprintf("in 4 Queues in 1 Queue Schdeuler\n");
#endif
  struct proc *p;
  struct cpu *c = mycpu();
  //int ct = 0;
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti(); //cpu가 apic로부터 TIMER INTERRUPT 를 받아 preemption을 가능하게 하기위한 sti()
    acquire(&ptable.lock); //병행성 문제를 해결하기 위한 ptable Lock (Spinlock : while 락)

    // Loop over process table looking for process to run.
    //RunQueue에서 우선순위 높은 친구 뽑아옴 (만약 같을 시 cpu 사용량 적은 친구로 뽑기..)
    if (!(p = getHighPri())) {
      release(&ptable.lock);
      continue;
    }


#ifdef DEBUG
    cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks\n",
       p->pid, p->priority, p->proc_tick, p->cpu_used);
#endif
#ifdef DEBUGS
    cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks (2)\n",
       p->pid, p->priority, p->proc_tick, p->cpu_used);
#endif
#ifdef ANALY
      acquire(&tickslock);
      cprintf("PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks, totalTicks : %d (2)\n",
              p->pid, p->priority, p->proc_tick, p->cpu_used, ticks);
      release(&tickslock);
#endif

    c->proc = p; //스케쥴링 뽑인 녀석을 cpu 의 프로세스로 설정
    switchuvm(p); //바뀐 p의 pagetable을 가져오는 함수
    p->state = RUNNING;
    swtch(&(c->scheduler), p->context); //CPU에게 현재 proc.c  schdeuler 스케쥴러에서 프로세스 context로 전환
    switchkvm(); // 스케쥴러로 돌아왔으므로 다시 Kernel Pagetable loading

    //현재 cpu에 재갱신 플래그 (Scheduler_flag 가 설정되어있는 경우)
    if (mycpu()->scheduler_flag)
    {
      mycpu()->scheduler_flag = 0; //플래그 끄기
      updateQueue(); //재갱신 진행시켜

      //Queue에서 삽입해야함 (바로 다시 스케쥴리을 할 것이기 때문)
#ifndef NEWS
    //RunQueue 용 Switch인 경우엔 재삽입을 위한 프로세스 삭제 (갱신된 후 다시 빼는 과정)
      deleteQueue(&RunQueue[p->priority/4], p);  
#else
      deleteQueue(&(RunQueue[p->priority/4].queue[p->priority % 4]),p);
#endif
      //다시 실행중이던 프로세스로 복귀 (해당 프로세스도 업데이트 됨)
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      //해당 프로세스가 종료하면 해당위치부터 실행 (추가하기전이랑 사실상 동일한 위치)
      switchkvm();
    } 
    c->proc = 0; 
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
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock)) //락을 가지고 있는지 확인
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1) //interrupt 불능화 시켜놨는데 1로설정됨?
    panic("sched locks");
  if(p->state == RUNNING) //process를 yield에서 RUNABLE로 바꿨는데 RUNNING인 경우 
    panic("sched running");
  if(readeflags()&FL_IF) 
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler); //스케쥴러로 복귀
  mycpu()->intena = intena;
}

void
update(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}


// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  appendProc(myproc());
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
    iinit(ROOTDEV);   //파일 시스템 디스크 첫번째 superblock 받아오기
    initlog(ROOTDEV); //journaling  기법으로 메타데이터 혹시 있는지 복원 (그리고 해당 블록초기화)
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  //cprintf("sleep start\n");
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
  // sleep 전의 락을 걸기위해 서로간의 약속된 lk락을 버리고 ptable락으로 설정
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
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
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {

      p->state = RUNNABLE;

      p->priority = getSmallestPri();
      if (p->pid == 0 || p->pid == 1 || p->pid == 2)
        p->priority = 99;
      appendProc(p);
      //appendProc(p);
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
        p->state = RUNNABLE;
        p->priority = getSmallestPri();
        if (p->pid == 0 || p->pid == 1 || p->pid == 2)
          p->priority = 99;
        /*
        else if (p->priority == 99)
          p->priority = 0;
        */
        appendProc(p);
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