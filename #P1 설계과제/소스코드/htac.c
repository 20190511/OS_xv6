#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#ifndef     NULL
#define     NULL    0
#endif

#define     BUFSIZE 512
char buf[512];
int line;

//Stack용 구조체
typedef struct stack {
    char strs[BUFSIZE]; 
    struct stack *next;
}Stack;

//#define DEBUG
/**
 *  htac 함수 호출 : cat를 전역변수 line만큼 반대로 호출
 *  @param fd   : File Descriptor
 *  @return 없음
 */
void
htac(int fd)
{
    int n;
    Stack   *head = (Stack*)malloc(sizeof(Stack)); //Stack 선언부
    head->next = NULL;
    Stack   *ptr = head, *tmp;               // Stack 포인터
    char    *remember = NULL, *last = NULL;  // 이전 문자열 기억하는 임시변수 
    char    tmp_line[BUFSIZE] = {0,};            // read간의 \n 단위로 짤린 문자열 처리
    while((n = read(fd, buf, BUFSIZE-1)) > 0) {
#ifdef DEBUG
        printf(1, "%s", buf);
#endif
        remember = buf;

        for (char* s = buf ; *s != '\0' ; s++) {
            //개행 문자 단위로 Stack에 요소 추가
            if (*s == '\n') {
                *s = '\0';  //문자열 토큰화
                Stack *newS = (Stack*)malloc(sizeof(Stack));
                char* s_ptr = newS->strs;

                //짤린 문자열 있으면 짤린 문자열 처리
                if (strlen(tmp_line) != 0) {
                    strcpy(s_ptr, tmp_line);
                    s_ptr += strlen(s_ptr); 
                    memset(tmp_line, 0, 512);
                }
                //Stack에 문자열 연결
                strcpy(s_ptr, remember);
                newS->next = ptr; //스택 연결
                ptr = newS;
#ifdef  DEBUG
                printf(1, "%s\n", remember);
                printf(1, "debug : %s\n", newS->strs);
#endif
                //remember 갱신
                remember = s+1;
            }
        }
        if (strlen(remember) > 0) {
            last = remember + strlen(remember) - 1;
            if (*last != '\n') {
                strcpy(tmp_line, remember);
            }
        }

        //쓰레기 코드 방지용 초기화
        memset(buf, 0, 512);
    }

    //혹시 끝 문자가 개행으로 끝나지 않아 잔여 문자열이 남은 경우 스택 연결
    if (strlen(tmp_line) != 0) {
        Stack *last_line = (Stack*)malloc(sizeof(Stack));
        strcpy(last_line->strs, tmp_line);
        memset(tmp_line, 0, 512);
        last_line->next = ptr; //스택 연결
        ptr = last_line;
    }
    

    //Stack 순회하며 line만큼 출력
    for (n = 0, tmp = ptr ; n < line ; n++, tmp = tmp->next) {
        if (tmp != head)
            printf(1, "%s\n", tmp->strs);
        else 
            break;
    } 

    //Scack 할당해제 함수
    for (tmp = ptr ; tmp != NULL ;) {
        ptr = tmp; //임시저장
        tmp = tmp->next; //
        free(ptr); 
    }
}

int 
main(int argc, char* argv[])
{
    int fd, i;
    // argc가 부족한 경우 에러처리
    if (argc <= 2) { 
        printf(2, "usage: %s <n> <FILE_NAME>\n", argv[0]); //표준 에러 처리
        exit();
    }
    // 연속 htac 지원을 위한 for문처리
    for (i = 1 ; i < argc ; i+=2) {
        line = atoi(argv[i]); //전역변수 line을 이용한 문자열처리
        //에러처리
        if ((fd = open(argv[i+1], O_RDONLY)) < 0) {
            printf(1, "htac: cannot open %s\n", argv[i+1]);
            exit();
        }
        htac(fd);  // htac함수호출
        close(fd); // 파일 디스크립터 해제
    }
    exit();
}