// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

// Read the super block.
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;
  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);
  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      // inode has no links and no other references: truncate and free.
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
/**
 * P4 과제를 위한 수정
 * bn은 블록번호를 가리킴.. -> 차례차례 접근하도록 설정
*/

static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;
  int idx_lvl1=-1, idx_lvl2, idx_lvl3;      //level 접근을 위한 인덱스
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;  //첫 번째 블록만 접근
  //level 1 블록
  if(bn < LEVEL1){
    /*
    int db=0;
    for (db = 0 ; db < 13 ; db++) 
      cprintf("ip->addrs : [%d]%d\n",db, ip->addrs[db]);
    */
    for (idx_lvl1 = NDIRECT ; bn >= NINDIRECT; idx_lvl1++, bn-=NINDIRECT);
    //디렉토리 할당부분
    if((addr = ip->addrs[idx_lvl1]) == 0) {
      ip->addrs[idx_lvl1] = addr = balloc(ip->dev); //디렉토리 생성에는 저널링을 안함
    }

    //디스크블록을 읽는 부분
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data; //데이터를 벡터화 (128 idx를 가지는 주소배열)
    if((addr = a[bn]) == 0){ //막상가봤더니 없네? -> 할당 후 log 재정리
      a[bn] = addr = balloc(ip->dev);
      log_write(bp); //저널링
    }
    brelse(bp); //저널링 풀기
    //cprintf("my block : idx1:%d, bn:%d\n", idx_lvl1, bn);
    return addr;
  }

  bn -= LEVEL1;       //3-LEVEL 메모리
  if (bn < LEVEL2) {
    for (idx_lvl2 = 6+4 ; bn >= NINDIRECT*NINDIRECT ; idx_lvl2++, bn-=NINDIRECT*NINDIRECT);
    //idx_lvl2 할당되었는지 확인 (가장 처음 프레임)
    if((addr = ip->addrs[idx_lvl2]) == 0)
      ip->addrs[idx_lvl2] = addr = balloc(ip->dev); //디렉토리 생성에는 저널링을 안함

    for (idx_lvl1=0 ; bn >= NINDIRECT ; idx_lvl1++, bn -=NINDIRECT);

    //idx_lvl1 할당되었는지 확인
    bp = bread(ip->dev, addr); //첫 번째 디렉토리 정보를 받아옴
    a = (uint*)bp->data; //데이터를 벡터화 (128 idx를 가지는 주소배열)
    if((addr = a[idx_lvl1]) == 0){ //막상가봤더니 없네? -> 할당 후 log 재정리
      a[idx_lvl1] = addr = balloc(ip->dev);
      log_write(bp); //저널링 -> 오류 생길 수 있음!
    }
    brelse(bp); //저널링 풀기

    //idx_lvl0 실제 데이터 블록부분 가져오기
    bp = bread(ip->dev, addr); //첫 번째 디렉토리 정보를 받아옴
    a = (uint*)bp->data; //데이터를 벡터화 (128 idx를 가지는 주소배열)
    if((addr = a[bn]) == 0){ //막상가봤더니 없네? -> 할당 후 log 재정리
      a[bn] = addr = balloc(ip->dev);
      log_write(bp); //저널링 -> 오류 생길 수 있음!
    }
    brelse(bp); //저널링 풀기
    return addr;
  }

  bn -= LEVEL2;       //4-LEVEL 메모리
  if (bn < LEVEL3) {
    //addr[12] 할당되었는지 확인 (가장 처음 프레임)
    idx_lvl3 = 6+4+2;
    if((addr = ip->addrs[idx_lvl3]) == 0)
      ip->addrs[idx_lvl3] = addr = balloc(ip->dev); //디렉토리 생성에는 저널링을 안함

    for (idx_lvl2=0 ; bn >= NINDIRECT*NINDIRECT ; idx_lvl2++, bn -=NINDIRECT*NINDIRECT);
    //idx_lvl2 할당되었는지 확인
    bp = bread(ip->dev, addr); //첫 번째 디렉토리 정보를 받아옴
    a = (uint*)bp->data; //데이터를 벡터화 (128 idx를 가지는 주소배열)
    if((addr = a[idx_lvl2]) == 0){ //막상가봤더니 없네? -> 할당 후 log 재정리
      a[idx_lvl2] = addr = balloc(ip->dev);
      log_write(bp); //저널링 -> 오류 생길 수 있음!
    }
    brelse(bp); //저널링 풀기

    for (idx_lvl1=0 ; bn >= NINDIRECT ; idx_lvl1++, bn-=NINDIRECT);
    //idx_lvl1 할당되었는지 확인
    bp = bread(ip->dev, addr); //첫 번째 디렉토리 정보를 받아옴
    a = (uint*)bp->data; //데이터를 벡터화 (128 idx를 가지는 주소배열)
    if((addr = a[idx_lvl1]) == 0){ //막상가봤더니 없네? -> 할당 후 log 재정리
      a[idx_lvl1] = addr = balloc(ip->dev);
      log_write(bp); //저널링 -> 오류 생길 수 있음!
    }
    brelse(bp); //저널링 풀기

    //idx_lvl0 실제 데이터 블록부분 가져오기
    bp = bread(ip->dev, addr); //첫 번째 디렉토리 정보를 받아옴
    a = (uint*)bp->data; //데이터를 벡터화 (128 idx를 가지는 주소배열)
    if((addr = a[bn]) == 0){ //막상가봤더니 없네? -> 할당 후 log 재정리
      a[bn] = addr = balloc(ip->dev);
      log_write(bp); //저널링 -> 오류 생길 수 있음!
    }
    brelse(bp); //저널링 풀기

    return addr;
  }
  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip)
{
  int i, j, l, m;
  struct buf *bp, *bp2, *bp3;
  uint *a, *a2, *a3;
#ifdef JHS
  int db = 0;
  for (db = 0 ; db < 13 ; db++) 
    cprintf("[%d] [%d]->%d\n", ip->inum, db, ip->addrs[db]);
#endif
  //1단계 Directing Mapping 할당해제
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){ //0,1,2,3,4,5 idx에대하여 할당해제
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  //6,7,8,9 에 해당하는 2-level mapping system 해제
  for (i = NDIRECT; i < NDIRECT + 4; i++) {
    if (ip->addrs[i]) {
      bp = bread(ip->dev, ip->addrs[i]); //페이지를 읽어옴
      a = (uint*)bp->data;
      //차례차례접근하며 페이지에 Indirecting 하게접근된 페이지 할당해제
      for (j = 0; j < NINDIRECT; j++) {
        if (a[j]) {
          bfree(ip->dev, a[j]);
        }
      }
      brelse(bp);
      //차례차례접근하며 페이지에 Indirecting 하게접근된 페이지 디렉토리 할당해제
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0; //다음 bmap을 위해 0으로 초기화
    }
  }
  //3-Level Mapping System 할당해제 () 10,11 idx
  for (i = NDIRECT+4 ; i < NDIRECT+6 ; i++) {
    if (ip->addrs[i]) {
      //3단계의 가장 초기 부모 맵 페이지 접근
      bp = bread(ip->dev, ip->addrs[i]);
      a = (uint*)bp->data;
      // 3단계의 2번째의 디렉토리 페이지 접근
      for (j = 0; j < NINDIRECT; j++) {
        //디렉토리 페이지가 있다면?
        if (a[j]) {
          //디렉토리 페이지를 접근하여 INDIRECING page 할당해제
          bp2 = bread(ip->dev, a[j]);
          a2 = (uint*)bp2->data ;
          for (l = 0 ; l < NINDIRECT ; l++) {
            if (a2[l]) {
              bfree(ip->dev, a2[l]);
            }
          }
          brelse(bp2); //a[j] 접근이 끝났으므로 버퍼 캐시에서 비워주기
          bfree(ip->dev, a[j]); //2번째 디렉토리 페이지를 할당해제
        }
      }
      brelse(bp);
      bfree(ip->dev, ip->addrs[i]); //1번째 디렉토리 페이지를 할당해제
      ip->addrs[i] = 0; //다음 bmap을 위해 0으로 초기화
    }
  }

  //4-Level Mapping System 할당해제 (12번 idx)
  i = NDIRECT+6;
  if(ip->addrs[i]){ //12번 idx가 할당되어있다면 4-Level Mapping간주
    if (ip->addrs[i]) {
      bp = bread(ip->dev, ip->addrs[i]); //4레벨의 첫번째 디렉토리 페이지 접근
      a = (uint*)bp->data;
      //하나씩 포인터로 연결해가며 4레벨의 두번째 디렉토리로 페이지 접근
      for (j = 0; j < NINDIRECT; j++) {
        if (a[j]) { //4레벨 디렉토리 중 2번째 디렉토리의 각 요소 포인터로 접근
          bp2 = bread(ip->dev, a[j]);
          a2 = (uint*)bp2->data;
          for (l = 0 ; l < NINDIRECT ; l++) {
            if (a2[l]) { //3번째 디렉토리 페이지가 존재한다면 마지막 진짜 Page 접근을 위해 접근
              bp3 = bread(ip->dev, a2[l]); 
              a3 = (uint*)bp3->data;
              //3번째 디렉토리에서 -> 실제 페이지 할당해제를 위한 Indirecting Mapping 할당해제
              for (m = 0 ; m < NINDIRECT ; m++) {
                if (a3[m]){
                  bfree(ip->dev, a3[m]); //각 실제요소 할당해제
                }
              }
              brelse(bp3); //더이상 해당 3번째 디렉토리 페이지 접근이 끝나서 캐시에서 비워주기
              bfree(ip->dev, a2[l]); //3번째 디렉토리 페이지를 할당해제
            }
          }
          brelse(bp2); //더이상 2번째 디렉토리 페이지 접근이 끝나서 캐시에서 비워주기
          bfree(ip->dev, a[j]); //2번째 디렉토리 페이지를 할당해제
        }
      }
      brelse(bp); //더이상 idx 12 접근이 없으므로 캐시에서 비워주기
      bfree(ip->dev, ip->addrs[i]); //1번째 디렉토리 (가장 앞단) 할당해제
      ip->addrs[i] = 0; //인덱스 초기화
    }
  }
  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
