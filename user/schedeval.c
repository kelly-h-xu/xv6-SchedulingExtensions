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
        printf("\n");
        } else {
            printf("failed to get proc info for pid %d\n", pid);
        }
}

void wait_for_all_children(void) {
    while (wait(0) > 0)
        ; // keep waiting until no more children exist
}

//Same as the accuracy test suite, sanity check that timings are working 
int sanity_check()
{
    printf("\n=== SANITY CHECK ===\n");

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
    wait(0);
    wait(0);
    return 0;
}

//Adversarial to Round Robin
int eval1()
{
    printf("\n=== TEST 1: MANY SHORT + ONE LONG WITHIN ===\n");

    // Define runtimes
    int rt[5] = {20, 20, 500, 20, 20};
    int pid[5];

    // Fork all processes
    for (int i = 0; i < 5; i++)
    {
        pid[i] = fork();
        if (pid[i] == 0)
        {
            // Required for STCF & SJF
            setexpected(rt[i]);

            // Required for STCF
            setstcfvals(rt[i]);

            // Simulated CPU work
            work(rt[i]);

            // Identify short vs long
            if (rt[i] < 100)
                printf("Child %d (pid=%d) SHORT job rt=%d done\n", i, getpid(), rt[i]);
            else
                printf("Child %d (pid=%d) LONG job rt=%d done\n", i, getpid(), rt[i]);

            // Print timing info
            print_info(getpid());

            exit(0);
        }
    }

    // Parent: collect completion order
    int finish[5];
    for (int i = 0; i < 5; i++)
        finish[i] = wait(0);

    printf("\n=== COMPLETION ORDER ===\n");
    for (int i = 0; i < 5; i++)
        printf("%d ", finish[i]);
    printf("\n");

    // printf("\nExpected order for SJF/STCF:\n");
    // printf("20 → 25 → 30 → 35 → 40 → 500\n\n");
    return 0;
}


// Adversarial to FIFO
// ------------------------------------------------------------
// TEST X: LONG JOB FIRST, THEN MANY SHORT JOBS
// One very long job starts, then several short jobs arrive later

// Expected completion (FIFO): long job first 
// Expected completion (SJF/STCF): short jobs first → long job last
// 
// ------------------------------------------------------------
int eval2()
{
    printf("\n=== TEST 2: LONG THEN MANY SHORT JOBS ===\n");

    // Start the long job first
    int p_long = fork();
    if (p_long == 0)
    {
        setexpected(500);
        setstcfvals(500);
        work(500);
        int pid = getpid();
        printf("LONG done (pid=%d)\n", pid);
        // Print timing info
        print_info(pid);
        exit(0);
    }

    // Delay before launching short jobs
    for (volatile int i = 0; i < 5000000; i++)
        ;

    int p_short[4];
    int runtimes[4] = {20, 20, 20, 20};

    // Launch several short jobs afterward
    for (int i = 0; i < 4; i++)
    {
        p_short[i] = fork();
        if (p_short[i] == 0)
        {
            setexpected(runtimes[i]);
            setstcfvals(runtimes[i]);
            work(runtimes[i]);
            int pid = getpid();
            printf("SHORT %d done (pid=%d)\n", i, pid);
            // Print timing info
            print_info(pid);
            exit(0);
        }
    }

    // Collect completion order
    int finish[5];
    for (int i = 0; i < 5; i++)
        finish[i] = wait(0);

    printf("Finish order:\n");
    for (int i = 0; i < 5; i++)
        printf("%d ", finish[i]);
    printf("\n");

    return 0;
}


// ------------------------------------------------------------
// TEST X: STARVATION OF LONG JOB
// Continuous stream of very short jobs, long job should starve
// Expected behavior:
//   - Long job gets delayed until shorts stop (STCF/SJF)
//   - Demonstrates need for priority boost or MLFQ
// ------------------------------------------------------------
int eval3()
{
    printf("\n=== TEST 3: STARVATION OF LONG JOB ===\n");

    // Create the long job first
    int p_long = fork();
    if (p_long == 0)
    {
        setexpected(500);    // Long job
        setstcfvals(500);
        work(500);           // Simulated CPU work
        printf("LONG done (pid=%d)\n", getpid());
        print_info(getpid());
        exit(0);
    }

    // Delay a little before starting the short job stream
    for (volatile int i = 0; i < 3000000; i++)
        ;

    // Launch a continuous stream of short jobs
    const int N_SHORT = 6;
    int rt[] = {10, 10, 10, 10, 10, 10};
    int pid_short[N_SHORT];

    for (int i = 0; i < N_SHORT; i++)
    {
        pid_short[i] = fork();
        if (pid_short[i] == 0)
        {
            setexpected(rt[i]);
            setstcfvals(rt[i]);
            work(rt[i]);
            printf("SHORT %d done (pid=%d)\n", i, getpid());
            print_info(getpid());
            exit(0);
        }

        // Small delay between short job arrivals to simulate "continuous" arrival
        for (volatile int j = 0; j < 1000000; j++)
            ;
    }

    // Parent waits for all jobs
    int finish[N_SHORT + 1];
    finish[0] = wait(0); // long job or first short (depending on scheduler)
    for (int i = 1; i <= N_SHORT; i++)
        finish[i] = wait(0);

    printf("\nCompletion order:\n");
    for (int i = 0; i <= N_SHORT; i++)
        printf("%d ", finish[i]);
    printf("\n");

    printf("Expected (SJF/STCF): short jobs first, long job last\n");
    printf("Demonstrates starvation of long job in STCF/SJF & MLFQ\n\n");

    return 0;
}



int main() {
   sanity_check();
   wait_for_all_children();

   eval1();
   wait_for_all_children();

   eval2();
   wait_for_all_children();



   exit(0);
}
