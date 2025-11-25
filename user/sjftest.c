#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Simple CPU-bound workload wrapper: yield() 'ticks' times.
void
work(int ticks)
{
  for (int i = 0; i < ticks; i++) {
    yield();
  }
}

// TEST 1: PREEMPTION BEHAVIOR
//
// Long runs first, short arrives later -> short should finish first
int
test_preempt(void)
{
  printf("\n=== TEST 1: PREEMPTION BEHAVIOR ===\n");

  int fds[2];
  if (pipe(fds) < 0) {
    printf("pipe failed\n");
    return 0;
  }
  int r = fds[0];
  int w = fds[1];

  int p_long = fork();
  if (p_long == 0) {
    // LONG child
    close(r);
    setexpected(400);
    work(400);
    char tag = 'L';
    write(w, &tag, 1);
    printf("LONG done (pid=%d)\n", getpid());
    close(w);
    exit(0);
  }

  // small head start for long
  for (volatile int i = 0; i < 2000000; i++)
    ;

  int p_short = fork();
  if (p_short == 0) {
    // SHORT child
    close(r);
    setexpected(20);
    work(20);
    char tag = 'S';
    write(w, &tag, 1);
    printf("SHORT done (pid=%d)\n", getpid());
    close(w);
    exit(0);
  }

  close(w);

  char order[2];
  if (read(r, &order[0], 1) != 1) {
    printf("read #1 failed\n");
    close(r);
    wait(0);
    wait(0);
    return 0;
  }
  if (read(r, &order[1], 1) != 1) {
    printf("read #2 failed\n");
    close(r);
    wait(0);
    wait(0);
    return 0;
  }
  close(r);

  printf("Completion tags (pipe order): %c then %c\n", order[0], order[1]);
  printf("Expected: S then L (short before long)\n");

  wait(0);
  wait(0);

  return (order[0] == 'S' && order[1] == 'L');
}

// TEST 2: MIXED BATCH (ALL ARRIVE TOGETHER)
//
// 3 children with runtimes = {80, 10, 40}
// All forked back-to-back, so they become ready around the same time.
// Expected SJF finish order: 10, then 40, then 80.
int
test_mixed_batch(void)
{
  printf("\n=== TEST 2: MIXED BATCH (ALL ARRIVE TOGETHER) ===\n");

  int rt[3]  = {80, 10, 40};
  int pid[3];

  int fds[2];
  if (pipe(fds) < 0) {
    printf("pipe failed\n");
    return 0;
  }
  int r = fds[0];
  int w = fds[1];

  for (int i = 0; i < 3; i++) {
    pid[i] = fork();
    if (pid[i] == 0) {
      close(r);
      setexpected(rt[i]);
      work(rt[i]);
      unsigned char tag = (unsigned char)i;
      write(w, &tag, 1);
      printf("Child%d done (pid=%d, rt=%d)\n", i, getpid(), rt[i]);
      close(w);
      exit(0);
    }
  }

  close(w);

  unsigned char tags[3];
  for (int i = 0; i < 3; i++) {
    if (read(r, &tags[i], 1) != 1) {
      printf("read #%d failed\n", i);
      close(r);
      for (int k = 0; k < 3; k++)
        wait(0);
      return 0;
    }
  }
  close(r);

  int f_rt[3];
  for (int i = 0; i < 3; i++) {
    int idx = (int)tags[i];
    if (idx < 0 || idx >= 3) {
      printf("bad tag %d\n", idx);
      for (int k = 0; k < 3; k++)
        wait(0);
      return 0;
    }
    f_rt[i] = rt[idx];
  }

  printf("Completion runtimes (pipe order): %d, %d, %d\n",
         f_rt[0], f_rt[1], f_rt[2]);
  printf("Expected SJF order: 10, 40, 80\n");

  for (int k = 0; k < 3; k++)
    wait(0);

  return (f_rt[0] == 10 && f_rt[1] == 40 && f_rt[2] == 80);
}

// TEST 3: STAGGERED ARRIVALS
//
// Long starts first, then medium, then short.
//   - When MED arrives, it should preempt LONG.
//   - When SHORT arrives, it should preempt MED.
// So final finish order should be: SHORT -> MED -> LONG.
int
test_arrivals(void)
{
  printf("\n=== TEST 3: STAGGERED ARRIVALS ===\n");

  int fds[2];
  if (pipe(fds) < 0) {
    printf("pipe failed\n");
    return 0;
  }
  int r = fds[0];
  int w = fds[1];

  int p_long = fork();
  if (p_long == 0) {
    close(r);
    setexpected(200);
    work(200);
    char tag = 'L';
    write(w, &tag, 1);
    printf("LONG done (pid=%d)\n", getpid());
    close(w);
    exit(0);
  }

  for (volatile int i = 0; i < 4000000; i++)
    ;

  int p_med = fork();
  if (p_med == 0) {
    close(r);
    setexpected(50);
    work(50);
    char tag = 'M';
    write(w, &tag, 1);
    printf("MED done (pid=%d)\n", getpid());
    close(w);
    exit(0);
  }

  for (volatile int i = 0; i < 4000000; i++)
    ;

  int p_short = fork();
  if (p_short == 0) {
    close(r);
    setexpected(10);
    work(10);
    char tag = 'S';
    write(w, &tag, 1);
    printf("SHORT done (pid=%d)\n", getpid());
    close(w);
    exit(0);
  }

  close(w);

  char order[3];
  if (read(r, &order[0], 1) != 1 ||
      read(r, &order[1], 1) != 1 ||
      read(r, &order[2], 1) != 1) {
    printf("read failed in test_arrivals\n");
    close(r);
    wait(0); wait(0); wait(0);
    return 0;
  }
  close(r);

  printf("Completion tags (pipe order): %c, %c, %c\n",
         order[0], order[1], order[2]);
  printf("Expected: S, M, L\n");

  wait(0);
  wait(0);
  wait(0);

  return (order[0] == 'S' && order[1] == 'M' && order[2] == 'L');
}

// TEST 4: COMPLEX MIXED RUNTIMES
//
// 10 children with diverse runtimes:
//   rt = {120, 5, 80, 20, 50, 15, 200, 40, 10, 30}
//
// All forked in a tight loop so they are essentially “available together”.
// Finishing runtimes should be non-decreasing (each next job that finishes 
// should not be shorter than any job that finished earlier).
int
test_mixed_complex(void)
{
  printf("\n=== TEST 4: COMPLEX MIXED RUNTIMES ===\n");

  int rt[10] = {120, 5, 80, 20, 50, 15, 200, 40, 10, 30};
  int pid[10];

  int fds[2];
  if (pipe(fds) < 0) {
    printf("pipe failed\n");
    return 0;
  }
  int r = fds[0];
  int w = fds[1];

  for (int i = 0; i < 10; i++) {
    pid[i] = fork();
    if (pid[i] == 0) {
      close(r);
      setexpected(rt[i]);
      work(rt[i]);
      unsigned char tag = (unsigned char)i;
      write(w, &tag, 1);
      exit(0);
    }
  }

  close(w);

  unsigned char tags[10];
  for (int i = 0; i < 10; i++) {
    if (read(r, &tags[i], 1) != 1) {
      printf("read #%d failed in mixed_complex\n", i);
      close(r);
      for (int k = 0; k < 10; k++)
        wait(0);
      return 0;
    }
  }
  close(r);

  int f_rt[10];
  for (int i = 0; i < 10; i++) {
    int idx = (int)tags[i];
    if (idx < 0 || idx >= 10) {
      printf("bad tag %d in mixed_complex\n", idx);
      for (int k = 0; k < 10; k++)
        wait(0);
      return 0;
    }
    f_rt[i] = rt[idx];
  }

  printf("Completion runtimes (pipe order):");
  for (int i = 0; i < 10; i++)
    printf(" %d", f_rt[i]);
  printf("\n");

  for (int i = 1; i < 10; i++) {
    if (f_rt[i] < f_rt[i - 1]) {
      printf("SJF VIOLATION at i=%d: %d < %d\n",
             i, f_rt[i], f_rt[i - 1]);
      for (int k = 0; k < 10; k++)
        wait(0);
      return 0;
    }
  }

  for (int k = 0; k < 10; k++)
    wait(0);

  return 1;
}

int
test_suite(void){

  int pass_pre = test_preempt();
  int pass_batch = test_mixed_batch();
  int pass_arr = test_arrivals();
  int pass_mix_c = test_mixed_complex();

  int total = pass_pre + pass_batch + pass_arr + pass_mix_c;

  printf("\n===== RESULTS =====\n");
  printf("Test 1 (Preemption):        %s\n", pass_pre   ? "PASS" : "FAIL");
  printf("Test 2 (Mixed batch):       %s\n", pass_batch ? "PASS" : "FAIL");
  printf("Test 3 (Arrivals):          %s\n", pass_arr   ? "PASS" : "FAIL");
  printf("Test 4 (Complex mixed set): %s\n", pass_mix_c ? "PASS" : "FAIL");

  printf("Passed %d / 4 tests.\n", total);

  return total;
}

// 
// MAIN
// 
int
main(void)
{
  printf("===== SJF TESTING =====\n");
  setexpected(1);

  int NUM_LOOPS = 100;

  int failed = 0;
  for (int testLoop = 1; testLoop <= NUM_LOOPS; ++testLoop){
    int suiteTotal = test_suite();
    if (suiteTotal != 4){
        failed = 1;
        break;
    }
  }

  if (failed == 0){
    printf("Test Suite passed 100 times.\n");
  }

  exit(0);
}