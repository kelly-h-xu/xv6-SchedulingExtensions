#include "user.h"
void work(int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        yield();  
    }
}

void print_info(int pid){
    struct procinfo info;
    if(getprocinfo(pid, &info) == 0){
        uint64 tat = info.etime - info.ctime;            // turnaround time
        uint64 wt  = tat - info.rtime;                  // waiting time
        uint64 rt  = info.stime - info.ctime;           // response time
        printf("pid: %d, ctime (creation time): %lu, stime (start time): %lu, rtime (runtime): %lu, etime (exit time): %lu, priority: %d, name: %s\n",
            info.pid, info.ctime, info.stime, info.rtime, info.etime, info.priority, info.name);
        printf("turnaround time %lu, waiting time %lu, response time %lu \n", tat, wt, rt);
        } else {
            printf("failed to get proc info for pid %d\n", pid);
        }
}

//just check procinfo works for one process
void simple_test(){
   printf("=== simple test with fork ===\n");
    int pid = fork();
    if(pid == 0){
        // child
        printf("Child starting: pid=%d\n", getpid());
        work(200);    // simulate CPU work
        printf("Child finishing: pid=%d\n", getpid());
        exit(0);
    } else {
        // parent
        print_info(pid);
        wait(0); 
    }
}

void fork_test_3()
{
    printf("=== Testing 3 forks with procinfo ===\n");

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

            // // print info before work
            // print_info(my_pid);

            work(my_work);

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
  //fork_test_3();
  exit(0);
}
