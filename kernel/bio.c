// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKETS 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // hash table for all bufs
  // the larger the better?
  struct buf bucket[BUCKETS];
  struct spinlock bcache_bucket_lock[BUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache_lock");
  for (int i=0; i<BUCKETS; ++i) {
    initlock(&bcache.bcache_bucket_lock[i], "bcache_bucket_lock");
  }

  // Create linked list of buffers
  for (int i=0; i<BUCKETS; ++i) {
    bcache.bucket[i].prev = &bcache.bucket[i];
    bcache.bucket[i].next = &bcache.bucket[i];
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.bucket[0].next;
    b->prev = &bcache.bucket[0];
    initsleeplock(&b->lock, "buffer");
    bcache.bucket[0].next->prev = b;
    bcache.bucket[0].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;


  // Is the block already cached?
  int bucket_id = blockno%BUCKETS;
  acquire(&bcache.bcache_bucket_lock[bucket_id]);
  for(b = bcache.bucket[bucket_id].next; b != &bcache.bucket[bucket_id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bcache_bucket_lock[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the unused buffer from the same bucket
  for(b = bcache.bucket[bucket_id].next; b != &bcache.bucket[bucket_id]; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.bcache_bucket_lock[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Recycle the unused buffer from other buckets
  for(int i=0; i<BUCKETS; ++i){
    release(&bcache.bcache_bucket_lock[bucket_id]);
    if (i < bucket_id) {
      acquire(&bcache.bcache_bucket_lock[i]);
      acquire(&bcache.bcache_bucket_lock[bucket_id]);
    } else if (bucket_id < i) {
      acquire(&bcache.bcache_bucket_lock[bucket_id]);
      acquire(&bcache.bcache_bucket_lock[i]);
    } else {
      continue;
    }
    for(b = bcache.bucket[i].next; b != &bcache.bucket[i]; b = b->next){
      if(b->refcnt == 0) {
        // move the buffer to another bucket
        b->prev->next = b->next;
        b->next->prev = b->prev;
        bcache.bucket[bucket_id].next->prev = b;
        b->next = bcache.bucket[bucket_id].next;
        bcache.bucket[bucket_id].next= b;
        b->prev = &bcache.bucket[bucket_id];
        // update buf info
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bcache.bcache_bucket_lock[i]);
        release(&bcache.bcache_bucket_lock[bucket_id]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.bcache_bucket_lock[i]);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // refcnt != 0 at this point, so no race for b->blockno
  int bucket_id = b->blockno%BUCKETS;
  acquire(&bcache.bcache_bucket_lock[bucket_id]);
  --b->refcnt;
  release(&bcache.bcache_bucket_lock[bucket_id]);
}

void
bpin(struct buf *b) {
  int bucket_id = b->blockno%BUCKETS;
  acquire(&bcache.bcache_bucket_lock[bucket_id]);
  b->refcnt++;
  release(&bcache.bcache_bucket_lock[bucket_id]);
}

void
bunpin(struct buf *b) {
  // refcnt != 0 at this point, so no race for b->blockno
  int bucket_id = b->blockno%BUCKETS;
  acquire(&bcache.bcache_bucket_lock[bucket_id]);
  b->refcnt--;
  release(&bcache.bcache_bucket_lock[bucket_id]);
}


