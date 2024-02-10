#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_getpp()
{
  //memlayout.h 에서 사용자 영역 확인.
  pde_t *pgdir = myproc()->pgdir, pde;
  pde_t *pgtab;
  //사용자 영역은 0x00000000 ~ 0x80000000-1 까지임 (그 위로는 커널영역이 할당되어있음)
  //0x80000000 의 상위 10비트 -> 0x300, 중간 10비트 -> 0x400 까지로 계산
  //--> 0x1000 0000 0000 0000    0000 0000 0000 0000
  // 10 0000 0000 | 00 0000 0000 | PGE ==> PDE : 0x000 ~ 0x200-1 까지, PTE : 0x000 ~ 0x400-1 까지
  int pde_point = PDX(KERNBASE), pte_point = 0x400, i,j;
  int retVal = 0;
  for (i = 0 ; i < pde_point ; i++) { 
    if ((pde = pgdir[i]) & PTE_P) { //Page Directory가 할당되어있는지 확인
      pgtab = (pte_t*)P2V(PTE_ADDR(pde)); //Page Table 할당
      for (j = 0 ; j < pte_point ; j++) {
        if (pgtab[j] & PTE_P) {  //PTE가 할당되어있으면 페이지개수 증가
          retVal++;
        }
      }
    } 
  }
  return retVal; //물리페이지 개수 리턴
}

/**
 * 가상메모리 할당 -> 직적 내가 페이지테이블에 할당을 박아넣을까 고민..
 * 교수님의 의도는 가상페이지 테이블에 받은 인자에 대한 페이지를 연속적으로 할당하는 것을 생각하신듯함
 * 그리고 trap.c에서 page fault에 대하여 그떄서야 비로소 페이지테이블을 할당하라는 뜻 같음.
*/
int
sys_ssualloc()
{
  int size;
  int pagesize = PGSIZE;
  pde_t *pgdir = myproc()->pgdir, pde;
  pde_t *pgtab;
  argint(0, &size); //첫번째 인자에서 사이즈 받아오기

  //음수이거나 4의 배수 아니면 일단 에러처리 
  if (size < 0 || size % pagesize != 0)  
    return -1;
  //cprintf("%d\n",blockCount);


#ifdef ORIGIN //이전에 구현된 기법 (별로 좋지 못함 : 똑같이 동작하지만)
  //memlayout.h 에서 사용자 영역 확인.
  //사용자 영역은 0x00000000 ~ 0x40000000-1 까지임 (그 위로는 커널영역이 할당되어있음)
  //아래는 내가 임의로 페이지를 할당해서 집어넣는 과정
  int blockCount = size/pagesize; //할당받을 블록크기
  int pde_point = 1<<9, pte_point = 1<<10, i,j ,serial_count = 0;
  int save_i=-1, save_j=-1;
  for (i = 0 ; i < pde_point ; i++) {
    if ((pde = pgdir[i]) & (PTE_P)) {
      pgtab = (pte_t*)P2V(PTE_ADDR(pde)); //Page Table 할당
      for (j = 0 ; j < pte_point ; j++) {
          //LAZE 배치 뿐 아니라 P 비트가 할당안되면서 연속적인 공간을 찾아야함
        if (!(pgtab[j] & PTE_P) && !(pgtab[j] & PTE_LAZY)) { //단순히 물리메모리가 할당되었는지 판단 (PTE검사안해도 됨)
          if (serial_count == 0) {
            save_i = i;
            save_j = j; //시작위치기록
          }
          serial_count++;
        } else {
          if (serial_count > 0) {
            serial_count = 0;
            save_i = -1;
            save_j = -1;
          }
        }
        if (serial_count >= blockCount) {
          break;
        }
      }
    } 
  }

  //알맞은 공간을 못 찾았으면 pde 새로 하나 할당받아서 거기다 배치
  if (save_i == -1 || save_j == -1) {
    for (i = 0 ; i < pde_point ; i++) {
      if (!((pde = pgdir[i]) & PTE_P)) {
        if((pgtab = (pte_t*)kalloc()) < 0)
          return -1;
        pgdir[i] = V2P(&pgtab[i]) | PTE_P | PTE_W | PTE_U; //페이지테이블 할당
        save_i = i;
        save_j = 0;
      }
    }
  }

  //이제saved_i,j 에 가상페이지를 배치해보자
  pde = pgdir[save_i];
  pgtab = (pte_t*)P2V(PTE_ADDR(pde));
  for (j = save_j, i = save_i ; j < save_j + blockCount ; j++) {
    pgtab[j] = PTE_LAZY; //pteLazy하나 박아줌
  }
  myproc()->sz += size;
  return PGADDR(save_i, save_j, 0);

  //배치가 결정된 공간에 PTE_LAZY 설정하도록 하자
#else
  //아래부터는 vm.c 의 함수들을 이용하여 myproc()->sz 를 이용하여 페이지 위치를 찾는 과정
  //해당 방식이 원래 시스템과 더 잘 어울리며 더 안정적으로 돌아가도록 설계됨
  int sz = myproc()->sz;
  if (sz + size >= KERNBASE)
    return 0;
  uint allocAddr = PGROUNDUP(sz); //현재 sz위치에 페이지 할당 (새롭게 할당)
  uint returnAddress = allocAddr; //return 할 주소
  for (; allocAddr < sz + size ; allocAddr += PGSIZE)  { //allocuvm 참고
    pde = pgdir[PDX(allocAddr)]; //가상 페이지 확인
    //vm.c : walkpgdir 참고
    if (!(pde & PTE_P)) { //페이지 디렉토리 없으면 할당해야지?
      if((pgtab = (pde_t*)kalloc()) <0) { //페이지가 할당되지 않으면 에러처리
        cprintf("I want to Allocate PDE. But, the system didn't help me.. bye\n");
        return -1;
      }
      memset(pgtab, 0, PGSIZE);
      myproc()->pgdir[PDX(allocAddr)] = V2P(pgtab) | PTE_P | PTE_W | PTE_U;  //pgdir 할당
    }

    //이제saved_i,j 에 가상페이지를 배치해보자
    //vm.c : mappages() 참고
    pde = pgdir[PDX(allocAddr)]; //새롭게 할당받은 allocAddr의 페이지디렉토리 가져오기
    pgtab = (pte_t*)P2V(PTE_ADDR(pde)); //페이지 테이블 받아오기 (해당 PDE의)
    pgtab[PTX(allocAddr)] = PTE_LAZY; //나중에 물리메모리를 할당하겠다는 플래그를 박아넣음 
    //(진짜 페이지폴트인지 Lazy Allocation 인지 확인용)
  }
  myproc()->sz = sz + size; //프로세스 사이즈 재갱신
  return returnAddress;
#endif

}

int
sys_getvp()
{
  //memlayout.h 에서 사용자 영역 확인.
  pde_t *pgdir = myproc()->pgdir, pde;
  pde_t *pgtab;
  //사용자 영역은 0x00000000 ~ 0x80000000-1 까지임 (그 위로는 커널영역이 할당되어있음) -> KERNBASE 매크로 참조
  int pde_point = PDX(KERNBASE), pte_point = 0x400, i,j; 
  int retVal = 0;
  for (i = 0 ; i < pde_point ; i++) {
    if ((pde = pgdir[i]) & PTE_P) { //페이지 디렉토리가 할당되어있는지 확인
      pgtab = (pte_t*)P2V(PTE_ADDR(pde)); //PDE에 할당된 Page Table 개수 확인
      for (j = 0 ; j < pte_point ; j++) {
        //PTE_P 뿐 아니라 PTE_LAZY (Lazy Allocation) 둘 다 가상메모리페이지 개수로 사용할 수 있음
        if ((pgtab[j] & PTE_P) ||(pgtab[j] & PTE_LAZY)) {
          retVal++;
        }
      }
    } 
  }
  return retVal; //가상 페이지 개수 리턴
  //프로세스 페이지테이블의 할당된 물리페이지 수를 의미하는듯
}