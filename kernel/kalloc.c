// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int cid = 0; cid < NCPU; ++cid)
    initlock(&kmem[cid].lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  struct run *r;
  p = (char*)PGROUNDUP((uint64)pa_start);

  for (int cid = 0; cid < NCPU; ++cid)
    acquire(&kmem[cid].lock);
  int i=0;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE, ++i) {
    r = (struct run*)p;
    // Fill with junk to catch dangling refs.
    memset(p, 1, PGSIZE);
    r->next = kmem[i%NCPU].freelist;
    kmem[i%NCPU].freelist = r;
  }
  printf("total %d physical pages\n", i);
  for (int cid = 0; cid < NCPU; ++cid)
    release(&kmem[cid].lock);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int cid = cpuid();
  pop_off();

  acquire(&kmem[cid].lock);
  r->next = kmem[cid].freelist;
  kmem[cid].freelist = r;
  release(&kmem[cid].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int cid = cpuid();
  pop_off();

  acquire(&kmem[cid].lock);
  r = kmem[cid].freelist;
  if(r)
    kmem[cid].freelist = r->next;
  else {
    // steal mem from other cpu
    for (int oth_cid=0; oth_cid < NCPU; ++oth_cid) {
      if (oth_cid == cid)
        continue;
      release(&kmem[cid].lock);
      // deadlokc avoid by acquire lock in order
      if (cid < oth_cid) {
        acquire(&kmem[cid].lock);
        acquire(&kmem[oth_cid].lock);
      } else {
        acquire(&kmem[oth_cid].lock);
        acquire(&kmem[cid].lock);
      }
      if (kmem[oth_cid].freelist) {
        kmem[cid].freelist = kmem[oth_cid].freelist;
        kmem[oth_cid].freelist = 0;
        release(&kmem[oth_cid].lock);
        r = kmem[cid].freelist;
        kmem[cid].freelist = r->next;
        break;
      }
      release(&kmem[oth_cid].lock);
    }
  }
  release(&kmem[cid].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
