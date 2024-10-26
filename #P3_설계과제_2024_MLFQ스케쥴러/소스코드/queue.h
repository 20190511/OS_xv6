#ifndef QUEUE_H
#define QUEUE_H

#ifndef NULL 
#define NULL (void*)0
#endif

#ifndef false
#define false 0
#endif

#ifndef true
#define true 1
#endif

#define DEBUGS false
#if DEBUGS
#include "types.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
struct {
  struct proc proc[NPROC];
} ptable;
#endif

#ifndef NPROC
#define NPROC 64
#endif

#define MAX_AGING 250

#define MLFQ_CNT 4
typedef struct node {
  struct proc* p;
  struct node* prev;
  struct node* next;
  int use_flag;
}node;

typedef struct queue {
  node* head;
  node* tail;
  int cnt;
}queue;

queue mlfq[MLFQ_CNT];
node nodes[NPROC];
node dummyNode[MLFQ_CNT*2]; //dummy node;


node* newNode(struct proc* p);
void dieNode(node* node);
void dieNodePid(int pid);

void init_mlfq() ;
void append_mlfq(struct proc* p, int level);
void pop_mlfq(int level, int pid);
node* take_mlfq(int level);

int reLevel(struct proc* p) ;
void printNode();


//Implementation

int level_limit(int lvl) {
  if (lvl < 0 || lvl > MLFQ_CNT) return -1;

  switch (lvl)
  {
  case 0:
    return 10; 
  case 1:
    return 20;
  case 2:
    return 40;
  case 3:
    return 80;
  }
  return -1;
}

int reLevel(struct proc* p) {

  int curlevel = p->q_level;
  int maxTimer = level_limit(curlevel);

  if (p->cpu_burst >= maxTimer) {

    if (p->set_time == -1) {
      cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/INFINITY)\n",
              p->pid, p->cpu_burst, p->q_level, p->end_time);
    }
    else {
      cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",
              p->pid, p->cpu_burst, p->q_level, p->end_time, p->set_time);
    }

#ifdef DEBUG
    if (p)
      cprintf("PID: %d, NAME: %s,\n", p->pid, p->name);
#endif

    p->cpu_burst = 0;
    curlevel++;
  }
  else
    return false;

  if (curlevel >= MLFQ_CNT) {
    curlevel = MLFQ_CNT-1;
  }

  p->q_level = curlevel;
  return true;
}


node* newNode(struct proc* p) {
  node* next = NULL;
  int i = p->pid % NPROC, cnt = 0;
  for (; cnt < NPROC ; i = (i+1)%NPROC, cnt++) {
    if (nodes[i].use_flag) continue;
    next = &nodes[i];
    break;
  }

  if (!next) return NULL;

  next->p = p;
  next->prev = NULL;
  next->next = NULL;
  next->use_flag = true;
  return next;
}

void dieNode(node* node) {
  node->use_flag = false;
  node->next = node->prev = NULL;
  node->p = NULL;
}


//dummy node 추가
void init_mlfq() {
  for (int i = 0 ; i < MLFQ_CNT ; i++) {
    node* d_head = &dummyNode[i*2];
    node* d_tail = &dummyNode[i*2+1];
    
    d_head->next = d_tail;
    d_tail->prev = d_head;
    mlfq[i].cnt = 0;
    mlfq[i].head = d_head;
    mlfq[i].tail = d_tail;
  }
}


void append_mlfq(struct proc* p, int level) {
  if (level < 0 || level > MLFQ_CNT) return; //잘못됨 뭔가

  queue* q = &mlfq[level];
  
  node* me = newNode(p);

  node* tail = q->tail;
  node* tail_prev = q->tail->prev;

  tail->prev = tail_prev->next = me;
  me->prev = tail_prev;
  me->next = tail;

  q->cnt++;
  me->use_flag = true;
}

//맨 앞의 process 가져옴 -> pop  with RUNNIG
node* take_mlfq(int level) {
  if (!mlfq[level].cnt) return NULL;

  node* candidate = mlfq[level].tail->prev;


  while (candidate != &dummyNode[level*2] && candidate->p->state != RUNNABLE) {
    candidate = candidate->prev;
  }

  if (!candidate->p || candidate->p->state != RUNNABLE) return NULL;
  
  return candidate;
}


int pop_mlfq_pid(int pid) {
  node *take = NULL;
  int breakFlag = false;
  for (int i = 0; i < MLFQ_CNT && !breakFlag; i++)
  {
    if (!mlfq[i].cnt)
      continue;
    queue *q = &mlfq[i];
    take = q->head->next;

    for (; take != q->tail; take = take->next)
    {
      if (take->p && take->p->pid == pid) {
        breakFlag = true;
        break;
      }
    }

    if (take == q->tail || !take->p || take->p->pid != pid)
      continue;
  }
  if (!take || !(take->p) || take->p->pid != pid) return false;
  node* next = take->next;
  node* prev = take->prev;
  next->prev = prev;
  prev->next = next;
  
  int lvl = take->p->q_level;
  mlfq[lvl].cnt--; 
  //cprintf("out pop_mlfq\n");
  dieNode(take);
  return true;
}

void pop_mlfq(int level, int pid) {

  if (!mlfq[level].cnt) {
    return;
  }

  queue* q = &mlfq[level];

  node* take = mlfq[level].head->next;
  for ( ; take != mlfq[level].tail ; take = take->next) {
    if (take->p && take->p->pid == pid) break;
  }

  if (take == mlfq[level].tail || !take->p || take->p->pid != pid) return;

  node* next = take->next;
  node* prev = take->prev;
  next->prev = prev;
  prev->next = next;
  dieNode(take);
  q->cnt--;
  //cprintf("out pop_mlfq\n");

  return; 
}

void aging() {
    for (int i = 0 ; i < MLFQ_CNT ; i++) {

      if (!mlfq[i].cnt) continue;

      for (node *ptr = mlfq[i].head->next ; ptr != &dummyNode[i*2+1] ; ) {
        if (ptr->p->state == RUNNABLE) {
          ptr->p->cpu_wait++;
        }
        else if (ptr->p->state == SLEEPING) {
          ptr->p->io_wait_time++;
        }

        if (ptr->p->pid == 1 || ptr->p->pid == 2) {
          ptr = ptr->next;
          continue;
        }

        struct proc* p = ptr->p;
        ptr = ptr->next;
        if (p->cpu_wait >= MAX_AGING || p->io_wait_time >= MAX_AGING) {
          p->io_wait_time = 0;
          p->cpu_wait = 0;
          if (i >= 1) {

            // 에이징? 커널 프린트
            cprintf("PID: %d Aging\n", p->pid);
            pop_mlfq(i, p->pid);
            p->q_level--;
            append_mlfq(p, i-1);
          }
        }
      }
    }
  
}

//Scheduler update 구현
void scheduler_update(struct proc* p) {
  int state = false;
  if (p->pid != 1 && p->pid != 2)
    state = pop_mlfq_pid(p->pid);

  if (state)
    append_mlfq(p, p->q_level);
  p->end_time = 0;
}



  char* cvtState(int v) {
    switch(v) {
    case UNUSED:
      return "Unused";
    case EMBRYO: 
      return "Embryo";
    case SLEEPING:
      return "sleeping";
    case RUNNABLE:
      return "runnable";
    case RUNNING:
      return "running";
    case ZOMBIE:
      return "Zombie";
    };

    return "??";
  }
  void printNode() {
    for (int i = 0 ; i < MLFQ_CNT ; i++) {
      cprintf("<<< %d >>> \n", i);

      if (!mlfq[i].cnt) continue;
      for (node *ptr = mlfq[i].head->next ; ptr != &dummyNode[i*2+1] ; ptr = ptr->next) {
        cprintf("%d(%s, flag=%d, i=%p) ", ptr->p->pid, cvtState(ptr->p->state), ptr->use_flag, ptr - nodes);
      }
      cprintf("\n");
    }

    cprintf("===========\n");
  }
#endif