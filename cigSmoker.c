#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <semaphore.h>

#define THD_NUM   3
#define SHOWSLOW  0
sem_t item, tmp, more;
int count;
int flags[3]={0,0,0};
pthread_t provider, smoker[THD_NUM];
const char* ch_item[] = {"paper", "match", "leaf"};

void* smoker_func(void* arg){
  int i = (int)arg;
  while (true) {
    sem_wait(&item);
    if (flags[(i - 1) % 3]&& flags[(i + 1) % 3]) {
      flags[(i - 1) % 3] = false;
      flags[(i + 1) % 3] = false;
      printf("Smoker_func : Smoking--> %s\n", ch_item[i%3]);
      while (count > 0) {
        /*
          while 부분에서 count-- 를 해버리면 아래 else{ } 부분에서 Thread 가 
          Semaphore 에서 빠져나오기 전에 while loop가 진행된 채로 Provider가 진행되고 데드락에 걸릴 수 있었습니다.
          (while 안에서 count-- 를 해놓고 + 아래 SLOWSHOW 를 하면 데드락에 안걸림을 확인했습니다.)
          그래서 count를 줄이는 부분을 Sempahore에서 꺠어난 후 자기가 직접 줄이도록 살짝 수정했습니다.
        */ 
        sem_post(&item); 
        sem_post(&tmp);
        sem_wait(&item);
      }
#if SHOWSLOW
      sleep(1);
#endif
      sem_post(&more);
    }
    else {
      count++;
      sem_post(&item);
      sem_wait(&tmp);
      //count 감소를 위한 부분
      sem_wait(&item);
      count--;
      sem_post(&item);
    }
  }
}

void* provider_func(void* arg) {
  int item1, item2;
  while (true) {
    item1 = rand() % 3;
    item2 = (item1 + 1) % 3;
    printf("Provider_func : made--> %s, %s\n", ch_item[item1], ch_item[item2]);
    flags[item1] = true;
    flags[item2] = true;
    sem_post(&item);
    sem_wait(&more);
  }
}

int main(void) {
    if(sem_init(&item,0,0)==-1){
        perror("error initilalizing semaphore\n");
    }
    if(sem_init(&tmp,0,0)==-1){
        perror("error initilalizing semaphore\n");
    }
    if(sem_init(&more,0,0)==-1){
        perror("error initilalizing semaphore\n");
    }
 
    pthread_create(&provider,0, provider_func, NULL);
    for (int thn = 1 ; thn < THD_NUM+1 ; thn++) {
      pthread_create(&smoker[thn],0, smoker_func, (void*)thn);
    }
    for(;;);

}
