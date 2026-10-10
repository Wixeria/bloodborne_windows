#define _POSIX_C_SOURCE 200809L
#include "runtime_clock.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pthread.h>

/* Reuse one timer per calling thread. winpthreads invokes the destructor on
 * thread exit, so repeated audio-thread creation does not leak kernel handles. */
static pthread_key_t timer_key;
static pthread_once_t timer_once=PTHREAD_ONCE_INIT;
static int timer_key_error;
static void destroy_timer(void *timer) { CloseHandle((HANDLE)timer); }
static void initialize_timer_key(void) { timer_key_error=pthread_key_create(&timer_key,destroy_timer); }
static HANDLE thread_timer(void) {
    if (pthread_once(&timer_once,initialize_timer_key) || timer_key_error) return NULL;
    HANDLE timer=(HANDLE)pthread_getspecific(timer_key);
    if (!timer) {
        timer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   TIMER_MODIFY_STATE|SYNCHRONIZE);
        if (!timer && GetLastError()==ERROR_INVALID_PARAMETER)
            timer=CreateWaitableTimerW(NULL,FALSE,NULL);
        if (timer && pthread_setspecific(timer_key,timer)) { CloseHandle(timer); return NULL; }
    }
    return timer;
}
#endif

uint64_t runtime_monotonic_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); abort(); }
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}

int runtime_sleep_until(uint64_t deadline) {
#ifdef _WIN32
    if (runtime_monotonic_ns()>=deadline) return 0;
    HANDLE timer=thread_timer();
    if (!timer) return EIO;
    for (;;) {
        uint64_t now=runtime_monotonic_ns();
        if (now>=deadline) return 0;
        uint64_t remaining=deadline-now;
        /* Relative FILETIME intervals are negative and measured in 100 ns.
           Round up and recheck the monotonic deadline after every wakeup. */
        LARGE_INTEGER due;
        due.QuadPart=-(LONGLONG)(remaining/100+(remaining%100!=0));
        if (!SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)) return EIO;
        if (WaitForSingleObject(timer,INFINITE)!=WAIT_OBJECT_0) return EIO;
    }
#else
    struct timespec end={(time_t)(deadline/UINT64_C(1000000000)),(long)(deadline%UINT64_C(1000000000))};
    int e;
    do { e=clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&end,NULL); } while (e==EINTR);
    return e;
#endif
}
