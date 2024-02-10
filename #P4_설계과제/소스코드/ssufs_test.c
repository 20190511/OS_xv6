#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define BSIZE 512

char buf[BSIZE];

void _error(const char *msg) {
	printf(1, msg);
	printf(1, "ssufs_test failed...\n");
	exit();
}

void _success() {
	printf(1, "ok\n");
}

void test(int ntest, int blocks) {
	char filename[16] = "file";
	int fd, i, ret = 0;

	filename[4] = (ntest % 10) + '0'; //파일 이름 설정

	printf(1, "### test%d start\n", ntest);
	//파일 생성여부 테스트
	printf(1, "create and write %d blocks...\t", blocks);
	fd = open(filename, O_CREATE | O_WRONLY);
	
	if (fd < 0)
		_error("File open error\n");

	//파일 생성을 위해 블록수만큼 write 시도 (실질적 파일 검사) -> bmap 검사
	for (i = 0; i < blocks; i++) {
		ret = write(fd, buf, BSIZE);
		if (ret < 0) break;
	}
	if (ret < 0)
		_error("File write error\n");
	else
		_success(); //파일 성공여부 출력

	printf(1, "close file descriptor...\t");
	
	//파일 디스크립터 닫기 시도
	if (close(fd) < 0)
		_error("File close error\n");
	else
		_success();

	//파일 열기와 읽기 시도 -> Filesystem으로 망가진게 아닌가 검사
	printf(1, "open and read file...\t\t");
	fd = open(filename, O_RDONLY);
	
	if (fd < 0)
		_error("File open error\n");

	//블록수만큼 파일 읽기 검사  -> 기본적으로 bmap함수랑 동일한 메커니즘
	for (i = 0; i < blocks; i++) {
		ret = read(fd, buf, BSIZE);
		if (ret < 0) break;
	}
	if (ret < 0)
		_error("File read error\n");

	//파일 닫기 시도	
	if (close(fd) < 0)
		_error("File close error\n");
	else
		_success();

	printf(1, "unlink %s...\t\t\t", filename);
	//파일 제거 시도 (블록할당해제 : itrunc 함수 검사)
	if (unlink(filename) < 0)
		_error("File unlink error\n");
	else
		_success();

	printf(1, "open %s again...\t\t", filename);
	fd = open(filename, O_RDONLY);
	//파일이 지워졌는데 남아있는지 검사 : unlink 검사	
	if (fd < 0) 
		printf(1, "failed\n");
	else
		printf(1, "this statement cannot be runned\n");

	printf(1, "### test%d passed...\n\n", ntest);
}

int main(int argc, char **argv)
{
	for (int i = 0 ; i < BSIZE; i++) {
		buf[i] = BSIZE % 10;
	}

	test(1, 5); //5번 직접블록검사
	test(2, 500); //6,7,8번 등 2-level 파일 시스템 검사
	test(3, 5000); //10,11 번 3-Level 파일 시스템 검사
	test(4, 50000);	 //12번까지 Write(시간 엄청걸림) : 4-Level 파일 시스템 검사
	exit();
}
