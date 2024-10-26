#include <stdio.h>
#include "queue.h"


void init() {
    struct proc *p;
    int pid = 1;
    for (p = ptable.proc ; p < &ptable.proc[NPROC] ; p++){
        if (pid % 2 ) {
            p->state = RUNNABLE;
        }
        p->pid = pid++;
    }
}
int main(void) {
    init();
    init_mlfq();

    append_mlfq(&ptable.proc[1], 0);
    append_mlfq(&ptable.proc[3], 0);
    append_mlfq(&ptable.proc[4], 0);
    append_mlfq(&ptable.proc[2], 0);
    printNode();

    node* next = take_mlfq(0);
    printf("take = %d\n", next->p->pid);
    pop_mlfq(0,2);
    printNode();
    pop_mlfq(0,5);
    printNode();

    return 0;
}