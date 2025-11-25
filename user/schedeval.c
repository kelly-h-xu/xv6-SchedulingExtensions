#include "user.h"
void work(int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        yield();  
    }
}

uint64 MICROSECONDS = 10;
uint64  MILLISECONDS = 10000;
uint64  scale;

void print_info(int pid){
    struct procinfo info;
    scale = MICROSECONDS;
    char units[] = "µs";
    if(scale == MILLISECONDS){
        strcpy(units, "ms");
    }
   
    if(getprocinfo(pid, &info) == 0){
        uint64 tat = (info.etime - info.ctime)/scale;            // turnaround time
        uint64 wt  = ((info.etime - info.ctime) - info.rtime)/scale;                  // waiting time
        uint64 rt  = (info.stime - info.ctime)/scale;           // response time
        printf("pid: %d, ctime (creation time): %lu, stime (start time): %lu, rtime (runtime): %lu, etime (exit time): %lu, priority: %d, name: %s\n",
            info.pid, info.ctime/scale, info.stime/scale, info.rtime/scale, info.etime/scale, info.priority, info.name);
        printf("turnaround time %lu %s, waiting time %lu %s, response time %lu %s \n", tat, units, wt, units, rt, units);
        } else {
            printf("failed to get proc info for pid %d\n", pid);
        }
}

int test_preempt()
{
    printf("\n=== TEST 1: PREEMPTION ===\n");

    int p_long = fork();
    if (p_long == 0)
    {
        setexpected(200);
        work(200);
        int pid = getpid();
        printf("LONG done (pid=%d)\n", pid);
        print_info(pid);
        exit(0);
    }

    for (volatile int i = 0; i < 3000000; i++)
        ; // small delay

    int p_short = fork();
    if (p_short == 0)
    {
        setexpected(20);
        work(20);
        int pid = getpid();
        printf("SHORT done (pid=%d)\n", pid);
        print_info(pid);
        exit(0);
    }

    int first = wait(0);
    int second = wait(0);

    printf("Finish #1: %d   (expected LONG)\n", first);
    printf("Finish #2: %d   (expected SHORT)\n", second);

    return (first == p_long);
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
   test_preempt();
  //simple_test();
  //fork_test_3();
  exit(0);
}
