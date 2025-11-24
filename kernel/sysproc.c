#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "procinfo.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0)
  {
    if (growproc(n) < 0)
    {
      return -1;
    }
  }
  else
  {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (killed(myproc()))
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_setexpected(void)
{
  int expected;
  argint(0, &expected);

  if (expected < 0)
    expected = 0;

  struct proc *p = myproc();

  acquire(&p->lock);
  p->expected_runtime = (uint64)expected;
  release(&p->lock);

  return 0;
}

uint64
sys_setstcfvals(void)
{
  int expected;
  argint(0, &expected);

  if (expected < 0)
    expected = 0;

  struct proc *p = myproc();

  acquire(&p->lock);
  p->expected_runtime = (uint64)expected;
  p->time_left = (uint64)expected + 1;
  release(&p->lock);

  printf("sys_setstcfvals called with %d\n", expected);

  return 0;
}

// NOTE: Needed to do this to have access to yield in the tests
uint64
sys_yield(void)
{
  yield();
  return 0;
}

// Need this to get procinfo from kernel side to user side 
uint64
sys_getprocinfo(void)
{
  int pid;
  struct procinfo info;
  struct proc *p;

  argint(0, &pid);

  p = getproc(pid); // find process by pid
  if(p == 0)
    return -1;

  acquire(&p->lock);
  info.pid = p->pid;
  info.state = p->state;
  info.rtime = p->rtime;
  info.expected_runtime = p->expected_runtime;
  info.time_left = p->time_left;
  info.priority = p->priority;
  info.queue_level = p->queue_level;
  info.time_slice = p->time_slice;
  safestrcpy(info.name, p->name, sizeof(info.name));
  release(&p->lock);

  // copy struct to user space
  struct procinfo *user_ptr;
  argaddr(1, (uint64*)&user_ptr);
  
  if(copyout(p->pagetable, (uint64)user_ptr, (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}