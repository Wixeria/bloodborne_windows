#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#endif

/* Game patches must be rebuilt before the next process opens its GPU device. */
volatile int runtime_restarting;
void runtime_restart(void) {
    fflush(NULL);
#ifdef _WIN32
    const char *launcher=getenv("BB_WINDOWS_RESTART");
    if (!launcher || strcmp(launcher,"1")) {
        puts("Runtime: settings saved; restart with run-windows.ps1 to apply game patches");
        return;
    }
    puts("Runtime: requesting restart from run-windows.ps1");
    fflush(NULL);
    /* ExitProcess/CRT exit can deadlock in DLL detach while GPU workers still run.
     * The parent waits for this process handle before opening the next GPU device. */
    TerminateProcess(GetCurrentProcess(),75); /* private launcher restart code */
    _exit(75); /* fallback if process termination failed */
#else
    puts("Runtime: restarting through run.sh");
    runtime_restarting=1;
    __sync_synchronize();
    syscall(SYS_close_range, 3u, ~0u, 0u);
    execlp("bash", "bash", "run.sh", (char *)NULL);
    perror("runtime_restart: exec");
    _exit(1);
#endif
}
