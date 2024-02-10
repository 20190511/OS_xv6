#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define FILENAME    "test.txt"
#define EXTRACTFILE "middlefile.txt"

#define START_SCHED "scheduler_test start"
#define END_SCHED   "end of scheduler_test"
#define INIT_PID    "(0)"
#define DIED_PID    "(3)"
#define FIND_PID    "(2)"
#define PID_TIMER   "[SET]"

#define DEBUG       1
#define LINEBUF     1024
#define MAX_PID     512

struct {
    int pid;
    int pnum_id;
    int sched_time;    //프로세스 예상 실행시간 
    int create_ticks;  //프로세스 생성 시점 Tick
    int start_ticks;   //프로세스 첫 스케쥴링 tick
    int end_ticks;     //프로세스 스케쥴링 종료 Tick
    int CallCount;     //프로세스 스케쥴링 호출횟수
    int priority;      //프로세스 세팅 우선순위

    int turnAroundTime; //Turn around time
    int responseTime;   //Response Time
}pidInfo[MAX_PID];

typedef struct q_ {
    int line;
    int commaCount;
    struct q_* next;
    char lineBuf[LINEBUF]; 
}q;

//char* findTick(char* line, char* filebuf);
void    printList(q* q); //디버그용 화면 출력
void    extractBuffer (q* q, FILE* fp);
q*      makeCSV (q* s); 
q*      findTotalTicks(FILE* fp); //결과 txt로부터 틱 분석 함수


int main(void)
{
    FILE* fp1, *fp2;
    char filebuf[LINEBUF];
    char csvname[LINEBUF];

    if ((fp1 = fopen (FILENAME,"w+")) == NULL) {
        fprintf(stderr, "fopen error : %s\n", FILENAME);
        exit(0);
    }
    
    system("make clean");
    system("make analy=1 qemu > test.txt");
    
    //fprintf(fp1, "scheduler_test\n");
    printf("input your csv file name : ");
    fgets(csvname, LINEBUF, stdin);
    strcpy(csvname + strlen(csvname) - 1, ".csv");

    if ((fp2 = fopen (csvname,"w")) == NULL) {
        fprintf(stderr, "fopen error : %s\n", EXTRACTFILE);
        exit(0);
    }

    q* tickQ = findTotalTicks(fp1);
    printList(tickQ);
    tickQ = makeCSV(tickQ);
    extractBuffer(tickQ, fp2);
    fclose(fp1);
    fclose(fp2);
    exit(0);
}

void    extractBuffer (q* s, FILE* fp) {
    printf("Extract file to : %d\n", fp->_fileno);
    q* delNode = s;
    while (s != NULL) {
        fprintf(fp, "%s\n", s->lineBuf);
        s->commaCount = 0;
        s->line = 0;
        memset(s->lineBuf, 0 , LINEBUF);
        delNode = s;
        s = s->next;
        free(delNode);
    }
}

q*    makeCSV (q* s)
{

    if (s == NULL) {
        fprintf(stderr, "queue is NULL\n");
        s = (q*)malloc(sizeof(q));
        s->line = 1;
        memset(s->lineBuf, 0, LINEBUF);
        strcpy(s->lineBuf, ",");
        s->commaCount = 1;
    }

    char title[LINEBUF] = "PNUM,Total_tick,,PNUM,PID,schedTime,TurnAround,Response,CallCount,CreateTick,firstSchedTick,EndSchedtick,priority";
    int commCount = 1;
    q* ptr = s, *saved = s;
    for (int i = 0 ; i < MAX_PID ; i++) {
        //pidInfo[i].end_ticks 가 0이면 측정할 프로세스대상이 아님 (init 혹은 sh 프로세스)
        if (pidInfo[i].pid != 0 && pidInfo[i].end_ticks != 0 && pidInfo[i].pnum_id != -1) {
            if (ptr == NULL)
            {
                ptr = (q*)malloc(sizeof(q));
                memset(ptr->lineBuf, 0, LINEBUF);
                ptr->commaCount = saved->commaCount - commCount;
                saved->next = ptr;
                ptr->line = saved->line + 1;
                ptr->next = NULL;
                for (int comm = 0; comm < commCount; comm++)
                    strcat(ptr->lineBuf, ",");
            }


            sprintf(ptr->lineBuf + strlen(ptr->lineBuf), ",,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                            pidInfo[i].pnum_id, pidInfo[i].pid, pidInfo[i].sched_time, 
                            pidInfo[i].turnAroundTime, pidInfo[i].responseTime, pidInfo[i].CallCount,
                            pidInfo[i].create_ticks, pidInfo[i].start_ticks, pidInfo[i].end_ticks,
                            pidInfo[i].priority
                            );
            saved = ptr;
            ptr = ptr->next;
        }
    }
    /*
    q* savedHead = s, *savedPrev = s, *ptr = s;
    const int commaCount = 6; //pnum, pid, schedTime, TurnAround, ResponseTime, CallCount
    int curPNUM = -1;
    for (int i = 0 ; i < MAX_PID ; i++) {
        //pidInfo[i].end_ticks 가 0이면 측정할 프로세스대상이 아님 (init 혹은 sh 프로세스)
        if (pidInfo[i].pid != 0 && pidInfo[i].end_ticks != 0 && pidInfo[i].pnum_id != -1) {
            if (curPNUM != pidInfo[i].pnum_id) { //head로 복귀
                curPNUM = pidInfo[i].pnum_id;
                for (; ptr != NULL ; ptr = ptr->next) {
                    for (int comm = 0 ; comm < commaCount ; comm++) 
                        strcpy(ptr->lineBuf, ",");
                    ptr->commaCount += commaCount;
                }
                sprintf(title + strlen(title), ",%s,%s,%s,%s,%s,%s",
                    "PNUM", "PID", "SchedTime", "TurnAround", "Response", "CallCount");
                ptr = savedHead;
            }
            //연결리스트가 끝났으면 연결
            if (ptr == NULL) { //리스트가 없으면 생성 후 콤마세팅
                ptr = (q*)malloc(sizeof(q));
                memset(ptr->lineBuf, 0, LINEBUF);
                ptr->line = savedPrev->line+1;
                ptr->next = NULL;
                ptr->commaCount = savedPrev->commaCount - commaCount;
                savedPrev->next = ptr;
                for(int comm = 0 ; comm < ptr->commaCount ; comm++)
                    strcat(ptr->lineBuf, ","); 
            }
            
            sprintf(ptr->lineBuf + strlen(ptr->lineBuf), ",%d,%d,%d,%d,%d,%d",
                        pidInfo[i].pnum_id, pidInfo[i].pid, pidInfo[i].sched_time, 
                        pidInfo[i].turnAroundTime, pidInfo[i].responseTime, pidInfo[i].CallCount);
            ptr->commaCount += commaCount;
            //출력할 정보, pnum
            savedPrev = ptr;
            ptr = ptr->next;
        }
    }
    */
    q* init = (q*)malloc(sizeof(q));
    init->line = 0;
    init->commaCount = -1;
    strcpy(init->lineBuf, title);
    init->next = s;
    return init;
}


void  printList(q* s) {
    for (q* ptr = s ; ptr != NULL ; ptr = ptr->next) {
        printf("[%d] %s\n", ptr->line, ptr->lineBuf);
    }

    for (int i = 0 ; i < 512 ; i++) {
        if (pidInfo[i].pid == 0)
            continue;
#if DEBUG
        printf("=============================\n");
        printf("[pid : %d] info\n", pidInfo[i].pid);
        printf("pnumID : %d\n", pidInfo[i].pnum_id);
        printf("create_ticks : %d\n", pidInfo[i].create_ticks);
        printf("start_ticks : %d\n", pidInfo[i].start_ticks);
        printf("end_ticks : %d\n", pidInfo[i].end_ticks);
        printf("\n");
        printf("Process Priority : %d\n", pidInfo[i].priority);
        printf("sched_time : %d\n", pidInfo[i].sched_time);
        printf("call_count : %d\n", pidInfo[i].CallCount);
        printf("Turnaround Time : %d\n", pidInfo[i].turnAroundTime);
        printf("Response   Time : %d\n", pidInfo[i].responseTime);
        printf("=============================\n\n");
#endif
    }

}

q* findTotalTicks(FILE* fp)
{
    q* head = NULL;
    q* tail = NULL;
    char tempbuf[LINEBUF] = {0,};
    int curPNUM = -1;

    //Dummy 노드 생성 
    head = (q*)malloc(sizeof(q));
    head->line = 0; 
    head->next = NULL;
    tail = head;

    while (!feof(fp)) {
        fgets(tempbuf, 1024, fp);
        if (strstr(tempbuf, START_SCHED)) { 
            sscanf(tempbuf, "scheduler_test start[PNUM:%d]", &curPNUM);
        }
        else if (strstr(tempbuf, INIT_PID)) { 
            int pid, ticks; 
            sscanf(tempbuf, "PID : %d, %d (0)", &pid, &ticks);
            pidInfo[pid].create_ticks = ticks;
            
        }
        else if (strstr(tempbuf,  FIND_PID))  {
            int pid, total_ticks = 0;
            int tmp1, tmp2, tmp3;
            sscanf(tempbuf, "PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks, totalTicks : %d (2)",
                &pid, &tmp1, &tmp2, &tmp3, &total_ticks);
                
            if (pidInfo[pid].pid == 0) {
                pidInfo[pid].pnum_id = curPNUM;
                pidInfo[pid].pid = pid;
                pidInfo[pid].start_ticks = total_ticks;
                pidInfo[pid].CallCount = 1;
            }
            else {
                pidInfo[pid].end_ticks = total_ticks; //계속 갱신하면 마지막에 갱신된 값이 마지막 스케쥴링 tick
                pidInfo[pid].CallCount++;
            }
        }
        else if (strstr(tempbuf, DIED_PID)) {
            int pid, ticks;
            int tmp1, tmp2, tmp3;
            sscanf(tempbuf, "PID : %d, priority : %d, proc_tick : %d ticks, total_cpu_usage : %d ticks, totalTicks : %d (3)",
                &pid, &tmp1, &tmp2, &tmp3, &ticks);
            pidInfo[pid].end_ticks = ticks;
            /*
            pidInfo[pid].turnAroundTime = pidInfo[pid].end_ticks - pidInfo[pid].create_ticks;
            pidInfo[pid].responseTime = pidInfo[pid].start_ticks - pidInfo[pid].create_ticks;
            */ 
            if (ticks != 0) {
                pidInfo[pid].turnAroundTime = pidInfo[pid].end_ticks - pidInfo[pid].create_ticks;
                pidInfo[pid].responseTime = pidInfo[pid].start_ticks - pidInfo[pid].create_ticks;
            }
            
        }
        else if (strstr(tempbuf, END_SCHED)) {
            int sched_end_ticks;
            sscanf(tempbuf, "end of scheduler_test[PNUM>%d] : %d ticks", &curPNUM, &sched_end_ticks);
            q* newNode = (q*)malloc(sizeof(q));
            sprintf(newNode->lineBuf, "%d,%d", curPNUM, sched_end_ticks);
            newNode->line = tail->line+1; //라인수 정해줌
            newNode->commaCount = 1;
            newNode->next = NULL;
            //연결리스트 재연결
            tail->next = newNode;
            tail = newNode;
            
            //newNode->lineBuf
            //end of scheduler_test[PNUM:3] : 302 ticks
        }
        else if (strstr(tempbuf, PID_TIMER)) {
            int pid, s_ticks, priority;
            sscanf(tempbuf, "[SET] pid : %d, priority : %d, schedule_ticks : %d", &pid, &priority, &s_ticks);
            pidInfo[pid].priority = priority;
            pidInfo[pid].sched_time = s_ticks;
            //[SET] pid : 4, schedule_ticks : 591
        }
        if (feof(fp))
            break;
    }
    return head->next;
}