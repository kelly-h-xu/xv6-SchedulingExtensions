#include "types.h"

// lightweight snapshot of proc, containing data to be printed for evaluation 
struct procinfo {
  int pid;
  int state;
  char name[16];
  uint64 ctime;
  uint64 etime;
  uint64 rtime;
  uint64 expected_runtime;
  uint64 time_left;
  int priority;
  int queue_level;
  int time_slice;
};