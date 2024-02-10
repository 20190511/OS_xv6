#include "types.h"
#include "stat.h"
#include "user.h"

#define PNUM    3

void scheduler_func(void)
{
    int i, pid;
    for (i = 0; i < PNUM+1; i++) {
        pid = fork();

        if (pid == 0) {
            set_sche_info(i+10, 110);
            while(1);
        }
    }

    for (i = 0 ; i < PNUM ; i++)
        wait();
    printf(1, "end of scheduler_test\n");

}
int main(void)
{
    scheduler_func();
    exit();
}
