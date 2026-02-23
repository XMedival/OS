#include "syscall.h"

static unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void _start(void) {
    long pid = getpid();

    char msg[] = "Hello from process X!\n";
    msg[20] = '0' + (char)pid;
    write(1, msg, strlen(msg));

    // Spin for a bit so the scheduler can demonstrate preemption
    for (volatile long i = 0; i < 5000000; i++);

    char bye[] = "Process X exiting\n";
    bye[8] = '0' + (char)pid;
    write(1, bye, strlen(bye));

    exit();
}
