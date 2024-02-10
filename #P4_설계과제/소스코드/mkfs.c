#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#define JH  0     //디버그용 매크로
#define MAXFILE_SIZE  (6 + NINDIRECT*4 + NINDIRECT*NINDIRECT*2 + NINDIRECT*NINDIRECT*NINDIRECT) //블록 총 개수

#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = FSSIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  // 1 fs block = 1 disk sector
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta;

  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2+nlog);
  sb.bmapstart = xint(2+nlog+ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 2; i < argc; i++){
#if JH
    printf("new file : %s\n", argv[i]);
#endif
    assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }

    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, argv[i], DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(freeblock);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  //buf 블럭 하나를 읽어와서 buf에 저장해주는 부분
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  //zero block 처리
  uchar buf[BSIZE]; //Zero Block 상한 정리
  int i, j, count = 0;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < MAXFILE_SIZE);
  //count 만큼 bmapstart 앞부분에 쓰기 작업은 계속함
  for (j = 0 ; count < used; j++) {
    bzero(buf, BSIZE); 
    //count < used 이면서 앞의 block bit를 하나씩 채움 
    //count는 블록을 쓸 때마다 하나씩 증가
    //bmap은 비트단위로 계산하기 때문에 비트연산자체는 동일하게 진행
    for (i = 0; i < BSIZE*8 && count < used; i++, count++) {
      buf[i / 8] = buf[i / 8] | (0x1 << (i % 8));
    }
    wsect(sb.bmapstart + j, buf); //bitmap 초기화 계속 설정
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

//inode 초기화할 때 사용하는 것으로 추측중
void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint tmp, tmp2, tmp3;
  //uint tmp1;
  uint indirect[NINDIRECT];
  uint indirect1[NINDIRECT];
  uint indirect2[NINDIRECT];
  /*
  uint indirect3[NINDIRECT];
  */
  uint x;

  int idx1 = NDIRECT;
  //n += 30;

  rinode(inum, &din);
  off = xint(din.size);
#if JH
  printf("Block Count : %u\n", off/BSIZE);
#endif
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);

    //직접 매핑할 때 호출되는 부분
    if(fbn < NDIRECT){
#if JH
      printf("first NDIRECT : %d\n", fbn);
#endif
      //xint -> addr[fbn] 블록의 주소를 Little Endian 으로 바꿔주는 과정
      if(xint(din.addrs[fbn]) == 0){ // 만약에 addrs 가 비어있으면 freeblock을 자동으로 할당해주는 부분
        din.addrs[fbn] = xint(freeblock++);
      }

      //freeblock을 할당하고 주소를 받아옴
      x = xint(din.addrs[fbn]);
    } 
    else if (fbn < (NDIRECT + LEVEL1)){ //2-level 매핑할 때 호출되는 부분
#if JH
      printf("1 LEVEL INDIRECT : %d\n", fbn);
#endif
      idx1 = NDIRECT + (fbn-NDIRECT) / NINDIRECT; //idx1 는 실제로는 몇 번쨰 indirect인가로 접근
      tmp = fbn - NDIRECT; //INDIRECT 블록번호를 tmp로 구해옴 -> 물론 128 * 4 개씩있으니까 나머지로 구해옴

      if(xint(din.addrs[idx1]) == 0){
        din.addrs[idx1] = xint(freeblock++);
      }
      //Indirect 블록을 읽기위해 데이터블록을 읽어옴
      rsect(xint(din.addrs[idx1]), (char*)indirect);
      //만약에 indirect부분이 없는 블록이었다?
      if(indirect[tmp%NINDIRECT] == 0){ //-> 해당 블록에 freeblock을 할당해줌
        indirect[tmp%NINDIRECT] = xint(freeblock++);
        //해당 indirect 페이지에 freeblock만큼 재갱신함 (위와달리 디스크에 포인터연결을 하니까 갱신해줘야됨)
        wsect(xint(din.addrs[idx1]), (char*)indirect);
      }
      x = xint(indirect[tmp%NINDIRECT]); //freeblock을 받아옴
    }
    //3단계
    else if (fbn < (NDIRECT + LEVEL1 + LEVEL2)) {
      idx1 = NDIRECT + 4 + (fbn-NDIRECT-NINDIRECT*4) / (NINDIRECT*NINDIRECT); //3-level Indirect 부분
      // tmp = ((0 ~ 2*N*N-1) / N) % N (첫 번째 INDIRECT 주소)
      // tmp2 = (0 ~ 2*N*N-1) % N; 
      tmp = ((fbn-NDIRECT-NINDIRECT*4) / NINDIRECT) % NINDIRECT; 
      tmp2 = (fbn-NDIRECT-NINDIRECT*4) % NINDIRECT; 
#if JH
      printf("2 LEVEL INDIRECT : %d\n", fbn);
      printf("2 LEVEL INDIRECT : idx1 = %d, tmp = %d, tmp2 = %d\n", idx1, tmp, tmp2);
#endif
      
      if (xint(din.addrs[idx1]) == 0) {
        din.addrs[idx1] = xint(freeblock++);
      }
      rsect(xint(din.addrs[idx1]), (char*)indirect); //1단계 디렉토리 확인
      if (indirect[tmp] == 0) { //없으니까 freeblock 할당하고 wsect
        indirect[tmp] = xint(freeblock++);
        //해당 indirect 페이지에 freeblock만큼 재갱신함 (위와달리 디스크에 포인터연결을 하니까 갱신해줘야됨)
        wsect(xint(indirect[tmp]), (char*)indirect);
      }

      rsect(xint(indirect[tmp]), (char*)indirect1); //2단계 디렉토리 확인
      if (indirect1[tmp2] == 0) { //없으니까 freeblock 할당하고 wsect
        indirect1[tmp2] = xint(freeblock++);
        //해당 indirect 페이지에 freeblock만큼 재갱신함
        wsect(xint(indirect1[tmp2]), (char*)indirect1);
      }
      x = xint(indirect1[tmp2]);
    }
    else { //4단계 디렉토리 (실제로 선 프로그램이 2기가? ㅋㅋㅋ)
      idx1 = 12; //어차피 마지막
      tmp = ((fbn-NDIRECT-NINDIRECT*4-NINDIRECT*NINDIRECT*2) / (NINDIRECT*NINDIRECT)); // 1단계 디렉토리 idx -> 얜 1개뿐이라 / 안해줘도됨
      tmp2 = ((fbn-NDIRECT-NINDIRECT*4-NINDIRECT*NINDIRECT*2) / (NINDIRECT)) % NINDIRECT;  // 2단계 디렉토리 idx
      tmp3 = (fbn-NDIRECT-NINDIRECT*4-NINDIRECT*NINDIRECT*2) % NINDIRECT; //3단계 디렉토리 주소
#if JH
      printf("3 LEVEL INDIRECT : %d\n", fbn);
      printf("3 LEVEL INDIRECT : idx1 = %d, tmp = %d, tmp2 = %d, tmp3 = %d\n", idx1, tmp, tmp2, tmp3);
#endif

      if (xint(din.addrs[idx1]) == 0) {
        din.addrs[idx1] = xint(freeblock++);
      }
      rsect(xint(din.addrs[idx1]), (char*)indirect); //1단계 디렉토리 확인
      if (indirect[tmp] == 0) { //없으니까 freeblock 할당하고 wsect
        indirect[tmp] = xint(freeblock++);
        //해당 indirect 페이지에 freeblock만큼 재갱신함 (위와달리 디스크에 포인터연결을 하니까 갱신해줘야됨)
        wsect(xint(indirect[tmp]), (char*)indirect);
      }

      rsect(xint(indirect[tmp]), (char*)indirect1); //2단계 디렉토리 확인
      if (indirect1[tmp2] == 0) { //없으니까 freeblock 할당하고 wsect
        indirect1[tmp2] = xint(freeblock++);
        //해당 indirect 페이지에 freeblock만큼 재갱신함
        wsect(xint(indirect1[tmp2]), (char*)indirect1);
      }

      rsect(xint(indirect1[tmp2]), (char*)indirect2); //3단계 디렉토리 확인
      if (indirect2[tmp3] == 0) { //없으니까 freeblock 할당하고 wsect
        indirect2[tmp3] = xint(freeblock++);
        //해당 indirect 페이지에 freeblock만큼 재갱신함
        wsect(xint(indirect2[tmp3]), (char*)indirect2);
      }
      x = xint(indirect2[tmp3]);
    }


    //무조건 실행해야하는 부분 -> x buf를 받아와서 p를 copy로 받아오고 옮겨놓는 과정
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
