#include "user.h"
void work(int ticks, int pid)
{
    struct procinfo info;
    for (int i = 0; i < ticks; i++)
    {
        yield();  // give up CPU to let scheduler run

        if (i % 2 == 0) { // print every 2 ticks to reduce spam
            if(getprocinfo(pid, &info) == 0){
                printf("pid=%d, tick=%d, rtime=%lu, priority=%d, queue_level=%d\n",
                       info.pid, i, info.rtime, info.priority, info.queue_level);
            }
        }
    }
}

void print_info(int pid){
    struct procinfo info;
    if(getprocinfo(pid, &info) == 0){
    printf("pid: %d, rtime: %lu, priority: %d, name: %s\n",
           info.pid, info.rtime, info.priority, info.name);
  } else {
    printf("failed to get proc info for pid %d\n", pid);
  }
}

//just check procinfo works for one process
void simple_test(){
    printf("simple test----------------");
    int pid = 1;
    print_info(pid);
}

void fork_test_3()
{
    printf("=== Testing 3 forks with live procinfo ===\n");

    int n = 3;
    int pids[n];
    (void)pids;
    int i;

    for (i = 0; i < n; i++) {
        int pid = fork();
        if (pid == 0) {
            // child process
            int my_work = (i + 1) * 5;  // local copy of workload
            int my_pid = getpid();
            printf("Child starting: pid=%d, work_ticks=%d\n", my_pid, my_work);

            // print info before work
            print_info(my_pid);

            // do work with intermediate prints
            work(my_work, my_pid);

            // print info after work
            print_info(my_pid);

            printf("Child finishing: pid=%d\n", my_pid);
            exit(0);
        } else {
            // parent stores child pid
            pids[i] = pid;
        }
    }

    // parent waits for all children to finish
    for (i = 0; i < n; i++) {
        wait(0);
    }

    printf("=== All children finished ===\n");
}


int main() {
  simple_test();
  fork_test_3();
  exit(0);
}
