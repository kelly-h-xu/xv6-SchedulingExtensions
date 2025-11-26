#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Adjust these to make sure processes drop to lower queues
#define BURN_LOW  200000000 
#define BURN_MED  1000000 

void burn_cpu(int intensity) {
    if (intensity == 0) return;
    volatile double x = 0;
    for(int i = 0; i < intensity; i++) {
        x += 1;
    }
}

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p); // Create the pipe

    // STEP 0: PREPARE THE LOCK (Fill the pipe)
    // xv6 pipe buffer is usually 512 bytes. We fill it completely.
    printf("Main: Filling pipe to force blocking later...\n");
    char buf[512];
    for(int i=0; i<512; i++) buf[i] = 'x';
    write(p[1], buf, 512); 
    // Now, any further write() to p[1] will BLOCK until someone reads.

    printf("Starting Priority Inversion Test (Reader/Writer)...\n");

    // 1. Create Low Priority Process (The Reader)
    // L holds the "key" (it can read to free up space)
    int pid_l = fork();
    if (pid_l == 0) {
        // Drop priority
        char c;
        read(p[0], &c, 0); // Indicate reader
        burn_cpu(BURN_LOW); 
        printf("L (Low): Priority dropped. I am the Reader.\n");

        // L is now Low priority. M is running. 
        // L tries to read. If PI works, L gets boosted by H.
        printf("L (Low): About to read (releasing lock)...\n");
        read(p[0], &c, 1); // Reads 1 byte, making space for H
        
        printf("L (Low): Finished reading.\n");

        burn_cpu(BURN_LOW*2);
        printf("L (LOW): Finished.\n");
        exit(0);
    }

    // 2. Create Medium Priority Process (The Distraction)
    int pid_m = fork();
    if (pid_m == 0) {
        burn_cpu(BURN_MED); // Drop to Medium
        sleep(10); // Give L a tiny head start to finish its burning
        
        printf("M (Med): Waking up to hog CPU...\n");
        
        // M hogs CPU. If L is Low, M runs forever.
        // If L is boosted to High, L interrupts M.
        burn_cpu(BURN_LOW); 
        
        printf("M (Med): Finished.\n");
        exit(0);
    }

    // 3. Create High Priority Process (The Writer)
    int pid_h = fork();
    if (pid_h == 0) {
        // H starts fresh (High Priority)
        sleep(20); // Wait for L and M to become Low/Med and start running

        printf("H (High): Attempting to write (Should Block)...\n");
        
        // PIPE IS FULL. This write MUST block.
        // Kernel should see: H (High) is sleeping on Pipe. 
        // Pipe is full. L (Low) is the potential reader? 
        // (Note: Standard xv6 doesn't track "who" will read, 
        // so this relies on your specific PI implementation 
        // knowing that L is the one holding the resource).
        write(p[1], "H", 1); 
        
        printf("H (High): Write successful! Finished.\n");
        exit(0);
    }

    wait(0);
    wait(0);
    wait(0);
    printf("Test finished.\n");
    exit(0);
}