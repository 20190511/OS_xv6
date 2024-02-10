#include "types.h"
#include "stat.h"
#include "user.h"

#define PNUM    3
#define SHORT   1

void scheduler_func(void)
{
    int pid;
    int i;

#if SHORT
    //스케쥴링 userprogram 시간을 줄이고 3개 fork만 조절하기 위해 특별히 제작
    printf(1, "start scheduler_test\n");
    pid = fork();
    if (pid == 0) {
        set_sche_info(1, 110);
        while(1);
    }
    pid = fork();
    if (pid == 0) {
        set_sche_info(10, 60);
        while(1);
    }
    pid = fork();
    if (pid == 0) {
        set_sche_info(11, 60);
        while(1);
    }
#else
    //일반적으로 선형적인 구조로 PNUM을 실행시키기 위해 제작
    printf(1, "start scheduler_test : PNUM > %d\n", PNUM);
    uint start, end;
    start = myticks();
    //int schedule_list[6] = {1, 300, 24, 600, 34, 600}; // 각 프로세스의 priority 및 종료 타이머
    //int schedule_list[6] = {1, 110, 10, 60, 11, 60}; // 각 프로세스의 priority 및 종료 타이머
    for (i = 0; i < PNUM; i++) {
        pid = fork();

        if (pid == 0) {
            set_sche_info(i+6*i/2, i*100);
            //set_sche_info(schedule_list[i*2], schedule_list[i*2+1]);
            while(1);
            exit();
        }
    }
#endif
    //남은 프로세스들을 기다리는 부분 (부모가 자식 기다림)
    for (i = 0; i < PNUM; i++) {
        wait();
    }
#if SHORT
    printf(1, "end of scheduler_test\n");
#else
    end = myticks(); //스케쥴링이 끝난 시점 틱 측정
    printf(1, "end of scheduler_test : %d ticks\n", end-start);
#endif

}

/**
 * pnum 만큼 3~pnum 만큼 연속적으로 fork하여 전체성능을 보는 함수
*/
void scheduler_func_V2(int pnum)
{
    printf(1, "scheduler_test start[PNUM:%d]\n", pnum);
    int pid;
    int i;
    uint start, end;
    start = myticks(); //시작 틱 측정
    //int schedule_list[6] = {1, 300, 24, 600, 34, 600}; // 각 프로세스의 priority 및 종료 타이머
    //int schedule_list[6] = {1, 110, 10, 60, 11, 60}; // 각 프로세스의 priority 및 종료 타이머
    for (i = 0; i < pnum; i++) {
        pid = fork();

        if (pid == 0) {
            set_sche_info(i+6*i/2, (i+1)*40); //priority가 대략 4씩 증가하도록 설정
            //set_sche_info(schedule_list[i*2], schedule_list[i*2+1]);
            while(1);
            exit();
        }
    }
    //남은 프로세스들을 기다리는 부분 (부모가 자식 기다림)
    for (i = 0; i < pnum; i++) {
        wait();
    }
    end = myticks(); //끝난 시점에서 tick 측정
    printf(1, "end of scheduler_test[PNUM>%d] : %d ticks\n",pnum, end-start);

}

void scheduler_testing(int N) {
    int i;
    for (i = 3 ; i < N ; i++) {
        printf(1, "\n\n\n========================================\n");
        scheduler_func_V2(i);
        printf(1, "========================================\n");
    }
}

/**
 * 2개는 CPU 위주 작업, 10개는 I/O (sleep) 위주 작업 구성
*/
void scheduler_testing_3(int cpu_process, int io_process) {
    uint start, end;
    int pnum = cpu_process + io_process;
    int pid, j, ticks;
    start = myticks(); //시작 tick 측정
    printf(1, "scheduler_test start[PNUM:%d]\n", pnum);
    //cpu위주 작업 while(1); 만 반복하는 부분
    for (j = 0 ; j < cpu_process ; j++) {
        pid = fork();
        if (pid == 0) {
            set_sche_info(80, 400);
            while(1); 
        }
    }

    //io 위주의 작업을 시행하는 부분
    for (j = 0 ; j < io_process ; j++) {
        pid = fork();
        if (pid == 0) {
            set_sche_info(2, 400); //실행시간을 400ticks으로 고정
            ticks = myticks();
            while(1) {
                //그냥 while(1)만 하면 1tick이 증가하기도 전에 io를 하러가는 점 방지코드
                if (ticks + 1 > myticks()) {
                    ticks = myticks();
                    sleep(5);
                }
            }
        }
    }
    for (j = 0 ; j < cpu_process + io_process ; j++) 
        wait();
    end = myticks();
    printf(1, "end of scheduler_test[PNUM>%d] : %d ticks\n",pnum, end-start);
}


/**
 * ticks 제한 없이 무한정 sleep하게 하는 경우
*/
void scheduler_testing_4(int cpu_process, int io_process) {
    //scheduler_testing_3 과 동일한데 i/o(sleep) 작업에서 무한 sleep() 이 발생할 수 있음
    uint start, end;
    int pnum = cpu_process + io_process; //PNUM 개수
    int pid, j;
    start = myticks(); //시작시점 tick 측정
    printf(1, "scheduler_test start[PNUM:%d]\n", pnum);
    for (j = 0 ; j < cpu_process ; j++) {
        pid = fork();
        if (pid == 0) {
            set_sche_info(80, 400); //CPU 위주 작업 세팅
            while(1); 
        }
    }

    for (j = 0 ; j < io_process ; j++) {
        pid = fork();
        if (pid == 0) {
            set_sche_info(2, 400); //IO 위주 작업 세팅
            while(1) {
                // 1tick 이 끝나기도 전에 sleep을 하러가는게 문제
                sleep(1);
            }
        }
    }
    for (j = 0 ; j < cpu_process + io_process ; j++) 
        wait();
    end = myticks(); //끝난 시점의 tick 측정
    printf(1, "end of scheduler_test[PNUM>%d] : %d ticks\n",pnum, end-start);
}

int main(void)
{
    scheduler_func();
    //scheduler_testing(20);
    //scheduler_testing_4(5,15);
    exit();
}
