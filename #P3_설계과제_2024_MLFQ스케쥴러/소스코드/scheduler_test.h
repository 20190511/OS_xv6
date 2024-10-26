#include "types.h"
#include "stat.h"
#include "user.h"

void test1(void) { 
    printf(1, "start schduler_test\n");
    int pid = getpid();
    printf(1, "PID: %d created\n", pid);
    set_proc_info(0,0,0,0, 500);
    while(1);
    printf(1, "end of schduler_test\n", pid);
}

void test2(void) { 
    printf(1, "start schduler_test\n");
    int pid = getpid();
    printf(1, "PID: %d created\n", pid);
    set_proc_info(1,0,0,0, 500);
    while(1);
    printf(1, "end of schduler_test\n", pid);
}

void test3(void) { 
    printf(1, "start schduler_test\n");
    int pid = getpid();

    for (int i = 0 ; i < 3 ;i++) {
        if (pid) {
            pid = fork();
        }
        
        if (!pid) {
            printf(1, "PID: %d created\n", getpid());
            set_proc_info(2, 0, 0, 0, 300);
            while (1)
                ;
        }
    }

    for (int i = 0 ; i < 3 ; i++) {
        wait();
        //printf(1, "%d is clear\n", i+1);
    }

    printf(1, "end of schduler_test\n", pid);
}
