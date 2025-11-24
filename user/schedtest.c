#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    printf("usage: sjfjob expected_ticks work_iters\n");
    exit(1);
  }

  int expected = atoi(argv[1]);
  int iters    = atoi(argv[2]);

  setexpected(expected);

  volatile int x = 0;
  for (int i = 0; i < iters; i++) {
    x += i;
  }

  // maybe print something to see order
  printf("job done: expected=%d iters=%d x=%d\n", expected, iters, x);
  exit(0);
}
