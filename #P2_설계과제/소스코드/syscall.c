#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "syscall.h"

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// Fetch the int at addr from the current process.
/**
 * systemcall 호출 등에 쓰이는 함수 인자를 받아오는 함수 (정수 전용)
 * @param addr : 받아올 인자 주소 --> 직접계산해서 넣어줘야함
 * @param ip   : 인자를 받을 call-by-reference 변수
 * @return 성공시 0 에러시 -1
 * @warning argint 내부에서 호출됨
 * @warning syscall.c
*/
int
fetchint(uint addr, int *ip)
{
  struct proc *curproc = myproc();

  if(addr >= curproc->sz || addr+4 > curproc->sz)
    return -1;
  *ip = *(int*)(addr);
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
/**
 * systemcall 호출 등에 쓰이는 함수 인자를 받아오는 함수 (문자열 전용)
 * @param addr : 받아올 인자 주소 --> 직접계산해서 넣어줘야함
 * @param pp   : 인자를 받을 call-by-reference 변수
 * @return 성공시 문자열 길이 에러시 -1
 * @warning argstr 내부에서 호출됨
 * @warning syscall.c
*/
int
fetchstr(uint addr, char **pp)
{
  char *s, *ep;
  struct proc *curproc = myproc();

  if(addr >= curproc->sz)
    return -1;
  *pp = (char*)addr;
  ep = (char*)curproc->sz;
  for(s = *pp; s < ep; s++){
    if(*s == 0)
      return s - *pp;
  }
  return -1;
}

// Fetch the nth 32-bit system call argument.
/**
 * n번째 정수형 파라미터를 *ip로 call-by-ref 방식으로 가져옴
 * @param n  : n번째 인자 (n=0부터 1번째 인자로 인식)
 * @param ip : n번째 파라미터 값을 가져올 변수 포인터
 * @return 성공시 0 에러시 -1
 * @warning 4 + 4*n 이므로 0-based으로 가져옴 (즉 n=0 이면 1번째 parameter를 가져옴)
 * @warning syscall.c
*/
int
argint(int n, int *ip)
{
  //4 + 4*n 이므로 0-based으로 가져옴 (즉 n=0 이면 1번째 parameter를 가져옴)
  return fetchint((myproc()->tf->esp) + 4 + 4*n, ip);
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
/**
 * n번째 주소정보를 **pp 로 call-by-ref 방식으로 가져옴
 * @param n  : n번째 인자 (n=0부터 1번째 인자로 인식)
 * @param pp : n번째 파라미터에 해당하는 주솟값을 가져올 call-by-ref 변수
 * @param size : 받아올 주소공간의 크기? --> 해당 파라미터 위치에서 + size 가 프로세스 메모리보다 큰지 체크해줌
 * @return 성공시 0 에러시 -1
 * @warning 4 + 4*n 이므로 0-based으로 가져옴 (즉 n=0 이면 1번째 parameter를 가져옴)
 * @warning syscall.c
*/
int
argptr(int n, char **pp, int size)
{
  int i;
  struct proc *curproc = myproc();

  //우선 주소정보를 정수형으로 받아옴 (32비트 주소체게라서 주소정보가져올 수 있음)
  if(argint(n, &i) < 0)
    return -1;
  // size가 초과하는지 체크
  if(size < 0 || (uint)i >= curproc->sz || (uint)i+size > curproc->sz)
    return -1;
  // 주소를 call-by-ref 형태로 넣어줌
  *pp = (char*)i;
  return 0;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
/**
 * n번째 파라미터를 문자열 (string) 형태로 pp에 call-by-ref 형태로 넣어줌
 * @param n  : n번째 인자 (n=0부터 1번째 인자로 인식)
 * @param pp : n번째 파라미터 값을 가져올 문자열 포인터(이중포인터 char* 의 call-by-ref)
 * @return 성공시 '문자열 크기' 에러시 -1
 * @warning 4 + 4*n 이므로 0-based으로 가져옴 (즉 n=0 이면 1번째 parameter를 가져옴)
 * @warning syscall.c
*/
int
argstr(int n, char **pp)
{
  int addr;
  //우선 문자열 포인터가 유효한지 검사 --> 문자열 포인터를 가져옴 (32비트체계)
  if(argint(n, &addr) < 0)
    return -1;
  //해당 포인터 위치의 값을 가져옴
  return fetchstr(addr, pp);
}

extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);
extern int sys_date(void);
extern int sys_alarm(void);

//시스템 콜 trap-table (전역변수)
static int (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_date]    sys_date,
[SYS_alarm]   sys_alarm,
};

/**
 * syscall 호출 할 때 trapframe 테이블에서 시스템콜을 찾아서 호출
 * @warning 프로세스 tf->eax = syscalls[num](); 으로 호출됨
 * @warning syscall.c
*/
void
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax; //현 프로세스에 걸려있는 시스템 콜 찾아내기
  //시스템 콜이 유효한 범위 내에 있으면 실행됨
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 여기 라인을 조정하면 모든 시스템 콜을 출력하게 해줄 수 있음
    curproc->tf->eax = syscalls[num]();
  } else {
    //시스템 콜 호출 실패 시 출력오류 출력
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}
