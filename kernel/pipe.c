#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open

  struct proc *writer_proc; 
  struct proc *reader_proc;
};

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;
  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nwrite = 0;
  pi->nread = 0;
  pi->writer_proc = 0;
  pi->reader_proc = 0;
  initlock(&pi->lock, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;

 bad:
  if(pi)
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if(writable){
    pi->writeopen = 0;
    pi->writer_proc = 0;
    wakeup(&pi->nread);
  } else {
    pi->readopen = 0;
    pi->reader_proc = 0;
    wakeup(&pi->nwrite);
  }
  if(pi->readopen == 0 && pi->writeopen == 0){
    release(&pi->lock);
    kfree((char*)pi);
  } else
    release(&pi->lock);
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);

  //record writer process
  pi->writer_proc = pr; 

  while(i < n){
    if(pi->readopen == 0 || killed(pr)){
      release(&pi->lock);
      return -1;
    }
    if(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
      wakeup(&pi->nread);

      if (pi->reader_proc) {
        acquire(&pr->lock);
        pr->waiting_for = pi->reader_proc; // Record dependency
        release(&pr->lock);
        
        priorities_reorient(pi->reader_proc); 

      }

      sleep(&pi->nwrite, &pi->lock);

      if (pr->waiting_for) {
        acquire(&pr->lock);
        pr->waiting_for = 0; // Clear dependency
        release(&pr -> lock);
        priorities_reorient(pi->reader_proc); 
      }

    } else {
      char ch;
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
        break;
      pi->data[pi->nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);

  return i;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  acquire(&pi->lock);

  pi->reader_proc = pr; 

  while(pi->nread == pi->nwrite && pi->writeopen){  //DOC: pipe-empty
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }

    // --- PRIORITY INHERITANCE START ---
    // If there is a known writer, we donate our priority to them
    // because they are holding the data we need.
    printf("Writer pipe %p \n", pi->writer_proc);
    if (pi->writer_proc) {
      //printf("Haven't acquired this lock \n");
      acquire(&pr->lock);
      //printf("Acquired this lock \n");
      pr->waiting_for = pi->writer_proc; // Record dependency
      release(&pr->lock);
      // Boost the writer's priority
      // (Assumes you have implemented this function in proc.c)
      priorities_reorient(pi->writer_proc); 
      
    }
    // --- PRIORITY INHERITANCE END ---

    sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep

    // --- PRIORITY INHERITANCE CLEANUP ---
    // We woke up. We are no longer strictly waiting on the lock/data 
    // in the same way (or we need to re-evaluate).
    if (pr->waiting_for) {
      acquire(&pr->lock);
      pr->waiting_for = 0; // Clear dependency
      release(&pr -> lock);
      priorities_reorient(pi->writer_proc); 
    }
    // --- CLEANUP END ---
  }
  for(i = 0; i < n; i++){  //DOC: piperead-copy
    if(pi->nread == pi->nwrite)
      break;
    ch = pi->data[pi->nread % PIPESIZE];
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1) {
      if(i == 0)
        i = -1;
      break;
    }
    pi->nread++;
  }
  wakeup(&pi->nwrite);  //DOC: piperead-wakeup
  release(&pi->lock);
  return i;
}
