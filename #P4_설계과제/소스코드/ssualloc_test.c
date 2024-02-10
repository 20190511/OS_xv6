#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(void)
{
	int ret;
	//가상메모리 검사 (물리메모리페이지와 가상메모리 체크)
	printf(1, "Start: memory usages: virtual pages: %d, physical pages: %d\n", getvp(), getpp()); 
	//가상메모리 할당 시도 -> 음수라서 에러처리
	ret = ssualloc(-1234);

	if(ret < 0) 
		printf(1, "ssualloc() usage: argument wrong...\n");
	else
		exit();

	//가상메모리 할당 시도 -> 페이지크기 배수가 아니라서 에러처리
	ret = ssualloc(1234);

	if(ret < 0)
		printf(1, "ssualloc() usage: argument wrong...\n");
	else
		exit();

	//가상메모리에만 페이지 1개 추가
	ret = ssualloc(4096);

	if(ret < 0 )
		printf(1, "ssualloc(): failed...\n");
	else {
		//가상메모리 만 1개추가 (물리메모리는 추가되면 안됨)
		printf(1, "After allocate one virtual page: virtual pages: %d, physical pages: %d\n", getvp(), getpp());
		char *addr = (char *) ret;

		//해당페이지 접근 후 물리메모리가 추가되는지 검사
		addr[0] = 'I';
		printf(1, "After access one virtual page: virtual pages: %d, physical pages: %d\n", getvp(), getpp());
	}

	//가상메모리 할당 : 페이지 3개 추가
	ret = ssualloc(12288);

	if(ret < 0 )
		printf(1, "ssualloc(): failed...\n");
	else {
		//가상메모리만 3개 추가되야함
		printf(1, "After allocate three virtual pages: virtual pages: %d, physical pages: %d\n", getvp(), getpp());
		char *addr = (char *) ret;

		//각 페이지마다 하나씩 추가되면서 물리메모리 개수가 1개씩 증가하는지 확인
		addr[0] = 'a';
		printf(1, "After access of first virtual page: virtual pages: %d, physical pages: %d\n", getvp(), getpp());
		addr[10000] = 'b';
		printf(1, "After access of third virtual page: virtual pages: %d, physical pages: %d\n", getvp(), getpp());
		addr[8000] = 'c';
		printf(1, "After access of second virtual page: virtual pages: %d, physical pages: %d\n", getvp(), getpp());
	}

	exit();
}
