#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

#ifndef SCHEDPOLICY
#define SCHEDPOLICY RR
#endif
enum sched_policy SCHED_POLICY = SCHEDPOLICY;

extern uint ticks;
extern struct spinlock tickslock;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

static const uint64 quantum[3] = {0.5*10000, 1*10000, 2*10000};  //Quantum in milleseconds

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // Initialize scheduling fields.
  p->ctime = getTime();
  p->etime = 0;
  p->rtime = 0;
  p->stime = 0;
  p->ltime = 0;
  p->expected_runtime = 0;
  p->time_left = 0;
  p->priority = 0;
  p->queue_level = 0;
  p->time_slice = quantum[0];
  p->demote = 0;
  p->waiting_for = 0;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;

  // Clear scheduling metadata.
  p->ctime = 0;
  p->etime = 0;
  p->rtime = 0;
  p->stime = 0;
  p->ltime = 0;
  p->expected_runtime = 0;
  p->priority = 0;
  p->queue_level = 0;
  p->time_slice = 0;

  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  uint64 time = getTime();
  p->ctime = time;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if (sz + n > TRAPFRAME)
    {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  np->expected_runtime = p->expected_runtime;
  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  uint64 time = getTime();
  uint64 elapsed = getTime() - p->ltime;
  
  // Account for elapsed time
  if (elapsed < p->time_slice) {
    p->time_slice -= elapsed;
  } else{
    p->time_slice = 0;
    p->demote = 1;
  }


  p->xstate = status;
  p->rtime += elapsed;  //cpu burst time tracking
  p->etime = time;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // Found one.
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock); // DOC: wait-sleep
  }
}

// Scheduling policies ----------------------

// default, round robin
static int
schedule_rr(struct cpu *c)
{
  struct proc *p;
  int found = 0;
  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);

    if (p->state == RUNNABLE)
    {
      p->state = RUNNING;
      p -> ltime = getTime();
      if (p->rtime == 0){
        p->stime = getTime();
      }
      c->proc = p;

      // printf("RR: running PID %d\n", p->pid);
      swtch(&c->context, &p->context);
      p->rtime += getTime() - p->ltime;

      c->proc = 0;
      found = 1;
    }
    release(&p->lock);
  }
  return found;
}

// FIFO
static int
schedule_fifo(struct cpu *c)
{
    struct proc *p;
    struct proc *selected = 0;  // process with earliest ctime
    int found = 0;

    // First pass: find the RUNNABLE process with the smallest ctime
    for(p = proc; p < &proc[NPROC]; p++) {
        if(p->state == RUNNABLE) {
            if (!selected || p->ctime < selected->ctime) {
                selected = p;
            }
        }
    }

    // If a process is found, acquire its lock and run it
    if (selected) {
        acquire(&selected->lock);

        // Make sure it's still runnable 
        if(selected->state == RUNNABLE) {
            selected->state = RUNNING;
            selected -> ltime = getTime();
            if (selected->rtime == 0){
              selected->stime = getTime();
            }
            c->proc = selected;

            swtch(&c->context, &selected->context);
            selected->rtime += getTime() - selected->ltime;
            c->proc = 0;
            found = 1;
        }

        release(&selected->lock);
    }

    return found;
}

// Shortest job first
static int
schedule_sjf(struct cpu *c)
{
  struct proc *best = 0;
  uint64 best_key = ~0ULL;
  uint64 best_ctime = ~0ULL;
  int best_pid = 0;

  for (struct proc *p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      // 0 hint means "no info" -> treat as very large.
      uint64 key = p->expected_runtime ? p->expected_runtime : ~0ULL;

      if (best == 0 ||
        key < best_key ||
        (key == best_key && (p->ctime < best_ctime ||
        (p->ctime == best_ctime && p->pid < best_pid)))
      ) {
        best = p;
        best_key = key;
        best_ctime = p->ctime;
        best_pid = p->pid;
      }
    }
    release(&p->lock);
  }

  if (best == 0)
    return 0;
  // If *all* RUNNABLE candidates had no hint, run RR this round.
  if (best_key == ~0ULL)
    return schedule_rr(c);

  acquire(&best->lock);
  if (best->state != RUNNABLE) {
    // Raced with another CPU; try again.
    release(&best->lock);
    return 0;
  }

  best->state = RUNNING;
  if (best->rtime == 0){
      best->stime = getTime();
  }
  c->proc = best;

  // printf("SJF: running PID %d\n", best->pid);
  best->ltime = getTime();
  swtch(&c->context, &best->context);
  best->rtime += getTime() - best->ltime;

  c->proc = 0;
  release(&best->lock);
  return 1;
}

// Shortest time to completion first
static int
schedule_stcf(struct cpu *c)
{
  struct proc *best = 0;
  uint64 best_key = ~0ULL;
  uint64 best_ctime = ~0ULL;
  int best_pid = 0;

  for (struct proc *p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == RUNNABLE)
    {
      uint64 key;
      if (p->expected_runtime == 0)
      {
        key = ~0ULL;
      }
      else
      {
        key = p->time_left;
      }

      // if (best != 0)
      // {
      //   printf("STCF pick: pid=%d time_left=%lu key=%lu\n",
      //          best->pid,
      //          (unsigned long)best->time_left,
      //          (unsigned long)best_key);
      // }

      if (best == 0 ||
          key < best_key ||
          (key == best_key && (p->ctime < best_ctime ||
                               (p->ctime == best_ctime && p->pid < best_pid))))
      {
        best = p;
        best_key = key;
        best_ctime = p->ctime;
        best_pid = p->pid;
      }
    }
    release(&p->lock);
  }

  if (best == 0)
    return 0;
  if (best_key == ~0ULL)
    return schedule_rr(c);

  acquire(&best->lock);
  if (best->state != RUNNABLE)
  {
    release(&best->lock);
    return 1;
  }

  best->state = RUNNING;
  if (best->rtime == 0){
      best->stime = getTime();
  }
  c->proc = best;

  best->ltime = getTime();
  swtch(&c->context, &best->context);
  best->rtime += getTime() - best->ltime;

  c->proc = 0;
  release(&best->lock);
  return 1;
}


//struct proc proc_prty1[NPROC];
//struct proc proc_prty2[NPROC];
//struct proc proc_prty3[NPROC];

//int prty_arr[3] = {1,2,4};

void
priorities_reorient(struct proc *p) 
{
  //printf("Got here \n");
  struct proc *q;
  int current_prio;
  int wait = 0;
  
  acquire(&p->lock);
  //printf("acquired \n");

  // 1. Determine the baseline priority.
  // If we are recalculating (propagate=0), we start from the process's 
  // natural MLFQ level (different from queue level)
  current_prio = p->priority; 
  struct proc *target_proc = 0;

  // 2. Scan for anyone waiting for ME to find the highest priority (lowest value)
  for (q = proc; q < &proc[NPROC]; q++) {
    if (q == p || q->state == UNUSED) continue;
    // We must acquire q->lock to read q->waiting_for safely
    acquire(&q->lock);

    if (q->waiting_for == p) {
      //printf("Yes there is potential \n");
      if (q->queue_level < current_prio) {
        wait = 1;
        target_proc = q;
        printf("KERNEL: Boosting PID %d (queue %d) to match PID %d (queue %d)\n", p->pid, current_prio, target_proc->pid, target_proc->queue_level);
        current_prio = q->queue_level;
      } 
    }
    release(&q->lock);
  }

  if (wait == 0) {
    printf("KERNEL: PID %d with previous queue %d gets new queue %d \n", p->pid, p->queue_level, current_prio);
  }
  // 3. Apply the new priority
  int old_prio = p->queue_level;
  p->queue_level = current_prio;
  
  // If we changed priority, reset time slice to be fair to the new queue
  if (p->queue_level != old_prio) {
      //printf("Change quantum \n");
      p->time_slice = quantum[p->queue_level];
  } 

  // 4. TRANSITIVE INHERITANCE (The Chain)
  // If p is waiting for someone else, we must push this priority changes down the line.
  struct proc *next_target = p->waiting_for;
  
  release(&p->lock);

  // If we boosted 'p', and 'p' is waiting for 'next_target', 
  // we must reorient 'next_target' too.
  if (next_target) {
      // Recursive call (or iterative). 
      priorities_reorient(next_target);
  }
}


uint64 starv_cut = 1000*10000;

void
starvation_clean(void)
{
  struct proc *p;
  uint64 time = getTime();

  // --- 1. Aging Step (prevent starvation)
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == RUNNABLE) {
      uint64 waited = time - p->etime;
      if (waited > starv_cut && p->queue_level > 0) { // waited > 200ms
        p->queue_level--;
        p->time_slice = quantum[p->queue_level];
        p->etime = time;
      }
    }
    release(&p->lock);
  }
}

//Get time for 
int startIndex[3] = {0,0,0};
// Multi level feedback queue
static int
schedule_mlfq(struct cpu *c)
{
  struct proc *p;
  struct proc *min_p;

  int found = 0;

  start_search:
  starvation_clean();

  for (int prty = 0; prty < 3; prty ++) {
    min_p = 0;

    for(p = proc; p < &proc[NPROC]; p++) {

      acquire(&p->lock);
      if(p -> queue_level == prty && p->state == RUNNABLE) {
        if (min_p == 0 || p->ltime < min_p->ltime) { //find last scheduled job
          if (min_p != 0) { 
            c -> intena = 0;
            release(&min_p -> lock);
          }
          min_p = p;
          continue;
        }
      }
      c -> intena = 0;
      release(&p -> lock);
    }
    if (min_p != 0) {
      //lock for min_p already acquired
      p = min_p;
      //printf("pid %d \n", p -> pid);

      if (p->state != RUNNABLE) {
        break;
      }
      p->state = RUNNING;
      c->proc = p;

      p -> ltime = getTime();

        //check if first schedule
        if (p -> stime == 0) {
          p -> stime = p -> ltime;
        }

        swtch(&c->context, &p->context);
        p->rtime += getTime()-p->ltime;

      if (p -> time_slice == 0 && p -> queue_level < 2) {
        printf("Demotion happened \n");
        p -> queue_level++;
        p -> time_slice = quantum[p -> queue_level];
        p -> demote = 0;

        release(&p -> lock);
        priorities_reorient(p); 
        acquire(&p -> lock);
      } 

      c->proc = 0;
      found = 1;
      c->intena = 0;
      release(&p->lock);
      goto start_search;
    }
    c->intena = 0;
  } 
  
  return found;
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

    int found = 0;

    switch (SCHED_POLICY)
    {
      case FIFO:
      {
        found = schedule_fifo(c);
        break;
      }
      case SJF:
      {
        found = schedule_sjf(c);
        break;
      }
      case STCF:
      {
        found = schedule_stcf(c);
        break;
      }
      case MLFQ:
      {
        found = schedule_mlfq(c);
        break;
      }
      default:
      {
        found = schedule_rr(c);
        break;
      }
    }

    if (found == 0)
    {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);

  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  
  uint64 time = getTime();
  uint64 elapsed = time - p->ltime;
  p -> etime = time;

  // Account for elapsed time
  if (elapsed < p->time_slice) {
    p->time_slice -= elapsed;
  } else {
    p->time_slice = 0;
    p->demote = 1;
  }
  
  p->state = RUNNABLE;

  if (p->time_left > 0)
  {
    p->time_left--;
  }

  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (first)
  {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1)
    {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Sleep on channel chan, releasing condition lock lk.
// Re-acquires lk when awakened.
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock); // DOC: sleeplock1
  release(lk);
 
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on channel chan.
// Caller should hold the condition lock.
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

//helper to getprocinfo
struct proc *
getproc(int pid)
{
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);
        if(p->pid == pid){
          release(&p->lock);
          return p;
        }
        release(&p->lock);
      }
    return 0;
}
