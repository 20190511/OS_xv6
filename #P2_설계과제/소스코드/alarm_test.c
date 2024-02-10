#include "types.h"
#include "user.h"
#include "date.h"

int main(int argc, char* argv[])
{
    int seconds; 
    struct rtcdate r;

    //오류 처리
    if (argc <= 1)
        exit();

    seconds = atoi(argv[1]); // 시간 인자받기
    alarm(seconds); //alarm 설정

    date(&r);
    printf(1, "SSU_Alarm Start\n"); //시작 출력
    printf(1, "Current time : %d-%d-%d %d:%d:%d\n", 
        r.year, r.month, r.day, r.hour, r.minute, r.second); //현재 시간 출력
    while(1)
        ;
    exit();
}