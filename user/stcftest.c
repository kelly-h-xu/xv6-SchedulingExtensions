#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// // NOTE: wrapper for yield to help w testingg
void work(int ticks)
{
    for (int i = 0; i < ticks; i++)
    {
        yield();
    }
}

// ------------------------------------------------------------
// TEST 1: PREEMPTION
// Long job runs first, short job arrives later → short must finish first
// ------------------------------------------------------------
int test_preempt()
{
    printf("\n=== TEST 1: PREEMPTION ===\n");

    int p_long = fork();
    if (p_long == 0)
    {
        setexpected(200);
        setstcfvals(200);
        work(200);
        printf("LONG done (pid=%d)\n", getpid());
        exit(0);
    }

    for (volatile int i = 0; i < 3000000; i++)
        ; // small delay

    int p_short = fork();
    if (p_short == 0)
    {
        setexpected(20);
        setstcfvals(20);
        work(20);
        printf("SHORT done (pid=%d)\n", getpid());
        exit(0);
    }

    int first = wait(0);
    int second = wait(0);

    printf("Finish #1: %d   (expected SHORT)\n", first);
    printf("Finish #2: %d   (expected LONG)\n", second);

    return (first == p_short);
}

// ------------------------------------------------------------
// TEST 2: MIXED ORDER
// runtimes = {80,10,40} → expected finish order = idx 1, idx 2, idx 0
// ------------------------------------------------------------
int test_mixed()
{
    printf("\n=== TEST 3: MIXED RUNTIMES ===\n");

    int rt[3] = {80, 10, 40};
    int pid[3];

    for (int i = 0; i < 3; i++)
    {
        pid[i] = fork();
        if (pid[i] == 0)
        {
            setexpected(rt[i]);
            setstcfvals(rt[i]);
            work(rt[i]);
            printf("Child%d done (pid=%d)\n", i, getpid());
            exit(0);
        }
    }

    int finish[3];
    finish[0] = wait(0);
    finish[1] = wait(0);
    finish[2] = wait(0);

    printf("Finish order: %d, %d, %d\n", finish[0], finish[1], finish[2]);
    printf("Expected: pid of rt=10 → rt=40 → rt=80\n");

    // find pid for 10, 40, 80
    int p10 = pid[1];
    int p40 = pid[2];
    int p80 = pid[0];

    return (finish[0] == p10 && finish[1] == p40 && finish[2] == p80);
}

// ------------------------------------------------------------
// TEST 3: STAGGERED ARRIVALS
// Long job starts first, then medium, then short
// Expected finish: short → medium → long
// ------------------------------------------------------------
int test_arrivals()
{
    printf("\n=== TEST 4: STAGGERED ARRIVALS ===\n");

    int p_long = fork();
    if (p_long == 0)
    {
        setexpected(200);
        setstcfvals(200);
        work(200);
        printf("LONG done (pid=%d)\n", getpid());
        exit(0);
    }

    for (volatile int i = 0; i < 4000000; i++)
        ;

    int p_med = fork();
    if (p_med == 0)
    {
        setexpected(50);
        setstcfvals(50);
        work(50);
        printf("MED done (pid=%d)\n", getpid());
        exit(0);
    }

    for (volatile int i = 0; i < 4000000; i++)
        ;

    int p_short = fork();
    if (p_short == 0)
    {
        setexpected(10);
        setstcfvals(10);
        work(10);
        printf("SHORT done (pid=%d)\n", getpid());
        exit(0);
    }

    int f1 = wait(0);
    int f2 = wait(0);
    int f3 = wait(0);

    printf("Finish order: %d, %d, %d\n", f1, f2, f3);
    printf("Expected: SHORT → MED → LONG\n");

    return (f1 == p_short && f2 == p_med && f3 == p_long);
}

int test_mixed_complex()
{
    printf("\n=== TEST 4: COMPLEX MIXED RUNTIMES ===\n");

    // 10 diverse runtimes
    int rt[10] = {
        120, 5, 80, 20, 50, 15, 200, 40, 10, 30};

    int pid[10];

    // Fork all 10 children
    for (int i = 0; i < 10; i++)
    {
        pid[i] = fork();
        if (pid[i] == 0)
        {
            setexpected(rt[i]);
            setstcfvals(rt[i]);
            work(rt[i]);
            exit(0);
        }
    }

    // NOTE: need this logic to keep track of all 10 processes and actually make sure they're correctly being ordered
    //  Record finishing order
    int finish[10];
    for (int i = 0; i < 10; i++)
        finish[i] = wait(0);

    printf("Finish order (PIDs):\n");
    for (int i = 0; i < 10; i++)
        printf(" %d", finish[i]);
    printf("\n");

    // --- Build (pid, runtime) pairs ---
    int f_rt[10];
    for (int i = 0; i < 10; i++)
    {
        // match finish[i] to its runtime
        for (int j = 0; j < 10; j++)
            if (finish[i] == pid[j])
                f_rt[i] = rt[j];
    }

    printf("Finish order (runtimes):\n");
    for (int i = 0; i < 10; i++)
        printf(" %d", f_rt[i]);
    printf("\n");

    // --- Check STCF correctness: f_rt[] must be non-decreasing ---
    for (int i = 1; i < 10; i++)
    {
        if (f_rt[i] < f_rt[i - 1])
        {
            printf("STCF VIOLATION at i=%d: %d < %d\n",
                   i, f_rt[i], f_rt[i - 1]);
            return 0; // FAIL
        }
    }

    return 1; // PASS
}

int test_stcf_vs_sjf_diff(void)
{
    printf("\n=== TEST 5: STCF vs SJF DIFFERENCE ===\n");

    int pipeA[2], pipeB[2];
    if (pipe(pipeA) < 0 || pipe(pipeB) < 0)
    {
        printf("pipe error\n");
        return 0;
    }

    int pidA = fork();
    if (pidA < 0)
    {
        printf("fork A failed\n");
        return 0;
    }
    if (pidA == 0)
    {
        // Child A: long job
        close(pipeA[1]); // only read end
        close(pipeB[0]);
        close(pipeB[1]);

        setexpected(200);
        setstcfvals(200);

        // burn 100 "ticks"
        work(100);

        char buf;
        // block here until parent writes
        if (read(pipeA[0], &buf, 1) != 1)
        {
            printf("A: read failed\n");
            exit(0);
        }

        // now finish remaining 100 "ticks"
        work(100);
        printf("A done (pid=%d)\n", getpid());
        exit(0);
    }

    int pidB = fork();
    if (pidB < 0)
    {
        printf("fork B failed\n");
        return 0;
    }
    if (pidB == 0)
    {
        // Child B: medium job
        close(pipeB[1]); // only read end
        close(pipeA[0]);
        close(pipeA[1]);

        setexpected(150);
        setstcfvals(150);

        // burn 20 "ticks"
        work(20);

        char buf;
        // block here until parent writes
        if (read(pipeB[0], &buf, 1) != 1)
        {
            printf("B: read failed\n");
            exit(0);
        }

        // now finish remaining ~130 "ticks"
        work(130);
        printf("B done (pid=%d)\n", getpid());
        exit(0);
    }

    // Parent: we only use write ends
    close(pipeA[0]);
    close(pipeB[0]);

    // Give both children time to reach their blocking reads.
    // We just yield a bunch to let them run.
    for (int i = 0; i < 2000; i++)
        yield();

    // Unblock both at (roughly) the same time
    if (write(pipeA[1], "x", 1) != 1)
        printf("parent: write A failed\n");
    if (write(pipeB[1], "y", 1) != 1)
        printf("parent: write B failed\n");

    close(pipeA[1]);
    close(pipeB[1]);

    int first = wait(0);
    int second = wait(0);

    printf("Finish order (PIDs): %d, %d\n", first, second);
    printf("Expected under STCF: A (pid=%d) then B (pid=%d)\n", pidA, pidB);

    if (first == pidA && second == pidB)
    {
        printf("TEST 5 RESULT: PASS (looks like STCF, not SJF)\n");
        return 1;
    }
    else
    {
        printf("TEST 5 RESULT: FAIL (behavior looks like SJF or incorrect STCF)\n");
        return 0;
    }
}

int main()
{
    printf("===== STCF TEST SUITE =====\n");

    int pass_pre = test_preempt();
    int pass_mix = test_mixed();
    int pass_arr = test_arrivals();
    int pass_mix_c = test_mixed_complex();
    int pass_diff = test_stcf_vs_sjf_diff();

    printf("\n===== RESULTS =====\n");
    printf("Test 1 (Preemption): %s\n", pass_pre ? "PASS" : "FAIL");
    printf("Test 2 (Mixed runtimes): %s\n", pass_mix ? "PASS" : "FAIL");
    printf("Test 3 (Arrivals): %s\n", pass_arr ? "PASS" : "FAIL");
    printf("Test 4 (Complex mixed): %s\n", pass_mix_c ? "PASS" : "FAIL");
    printf("Test 5 (STCF vs SJF diff): %s\n", pass_diff ? "PASS" : "FAIL");

    int total = pass_pre + pass_mix + pass_arr + pass_mix_c + pass_diff;
    printf("Passed %d / 5 tests.\n", total);

    exit(0);
}