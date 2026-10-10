/* libkernel/libScePosix process services: clocks, sleeping, errno mapping,
 * pthread once/keys, signal bookkeeping and a narrow sysctl. Guest values
 * use FreeBSD numbering; host errno values never reach the guest directly. */
#define _GNU_SOURCE
#define _CRT_RAND_S
#include "runtime.h"
#include "runtime_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/random.h>
#include <sys/resource.h>
#endif
#include <sys/time.h>
#include <x86intrin.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define PAGE 16384

typedef struct { int64_t sec, nsec; } GuestTimespec;
typedef struct { int64_t sec, usec; } GuestTimeval;

int32_t runtime_guest_errno(int e) {
    switch (e) {
    case 0: return 0;
    case EPERM: return 1; case ENOENT: return 2; case ESRCH: return 3; case EINTR: return 4;
    case EIO: return 5; case ENXIO: return 6; case E2BIG: return 7; case ENOEXEC: return 8;
    case EBADF: return 9; case ECHILD: return 10; case EDEADLK: return 11; case ENOMEM: return 12;
    case EACCES: return 13; case EFAULT: return 14; case EBUSY: return 16; case EEXIST: return 17;
    case EXDEV: return 18; case ENODEV: return 19; case ENOTDIR: return 20; case EISDIR: return 21;
    case EINVAL: return 22; case ENFILE: return 23; case EMFILE: return 24; case ENOTTY: return 25;
    case EFBIG: return 27; case ENOSPC: return 28; case ESPIPE: return 29; case EROFS: return 30;
    case EMLINK: return 31; case EPIPE: return 32; case ERANGE: return 34; case EAGAIN: return 35;
    case ENAMETOOLONG: return 63; case ENOTEMPTY: return 66; case ETIMEDOUT: return 60;
    case ELOOP: return 62; case ENOSYS: return 78; case EOVERFLOW: return 84; case ECANCELED: return 85;
    default: return 5; /* EIO: unmapped host error */
    }
}
static int32_t fail_posix(int e) { *runtime_errno()=runtime_guest_errno(e); return -1; }

/* ---- clocks and sleeping ---- */
static uint64_t process_start;
__attribute__((constructor)) static void remember_start(void) { process_start=runtime_monotonic_ns(); }
static int host_clock(uint32_t id,clockid_t *out) {
    switch (id) {
    case 0: case 9: case 10: *out=CLOCK_REALTIME; return 1;            /* REALTIME(_PRECISE/_FAST) */
    case 4: case 11: case 12: case 5: case 7: case 8: *out=CLOCK_MONOTONIC; return 1; /* MONOTONIC/UPTIME */
    case 13: *out=CLOCK_REALTIME; return 1;                    /* SECOND */
    case 14: *out=CLOCK_THREAD_CPUTIME_ID; return 1;
    case 2: case 15: *out=CLOCK_PROCESS_CPUTIME_ID; return 1;         /* PROF/PROCTIME */
    case 16: case 17: case 18: case 19: *out=CLOCK_MONOTONIC; return 1; /* PS4 network clocks */
    default: return 0;
    }
}
static int clock_read(uint32_t id,GuestTimespec *ts) {
    clockid_t host;
    if (!ts || !host_clock(id,&host)) return EINVAL;
    struct timespec t;
    if (clock_gettime(host,&t)) return errno;
    if (id==13) t.tv_nsec=0;
    ts->sec=t.tv_sec; ts->nsec=t.tv_nsec; return 0;
}
static ABI int32_t kernel_clock_gettime(uint32_t id,GuestTimespec *ts) {
    int e=clock_read(id,ts); return e ? ERR(runtime_guest_errno(e)) : 0;
}
static ABI int32_t posix_clock_gettime(uint32_t id,GuestTimespec *ts) {
    int e=clock_read(id,ts); return e ? fail_posix(e) : 0;
}
static ABI int32_t posix_clock_getres(uint32_t id,GuestTimespec *ts) {
    clockid_t host; struct timespec t;
    if (!host_clock(id,&host)) return fail_posix(EINVAL);
    if (clock_getres(host,&t)) return fail_posix(errno);
    if (ts) { ts->sec=t.tv_sec; ts->nsec=t.tv_nsec; }
    return 0;
}
static ABI uint64_t process_time(void) { return (runtime_monotonic_ns()-process_start)/1000; }
static ABI uint64_t process_time_counter(void) { return runtime_monotonic_ns()-process_start; }
static ABI uint64_t process_time_frequency(void) { return 1000000000; }
static ABI uint64_t read_tsc(void) { return __rdtsc(); }
static uint64_t tsc_hz;
static pthread_once_t tsc_once=PTHREAD_ONCE_INIT;
static void calibrate_tsc(void) {
    uint64_t start=runtime_monotonic_ns(), t0=__rdtsc();
    runtime_sleep_until(start+20000000);
    uint64_t elapsed=runtime_monotonic_ns()-start;
    tsc_hz=(__rdtsc()-t0)*1000000000/(elapsed ? elapsed : 1);
}
static ABI uint64_t tsc_frequency(void) { pthread_once(&tsc_once,calibrate_tsc); return tsc_hz; }
/* Shared with the GPU library so flip/label timestamps use the guest's clock. */
uint64_t runtime_process_time_us(void) { return process_time(); }
uint64_t runtime_process_time_counter(void) { return process_time_counter(); }
uint64_t runtime_tsc_frequency(void) { return tsc_frequency(); }
static _Atomic uint64_t sleep_calls, sleep_elapsed_ns;
void runtime_sleep_stats(uint64_t *calls, uint64_t *ns) {
    *calls=atomic_exchange(&sleep_calls,0);
    *ns=atomic_exchange(&sleep_elapsed_ns,0);
}
static int sleep_ns(uint64_t ns) {
    uint64_t now=runtime_monotonic_ns();
    if (ns>UINT64_MAX-now) return EINVAL;
    const uint64_t start=runtime_wait_clock();
    const int result=runtime_sleep_until(now+ns);
    if (start) {
        const uint64_t elapsed=runtime_wait_clock()-start;
        atomic_fetch_add(&sleep_calls,1);
        atomic_fetch_add(&sleep_elapsed_ns,elapsed);
        runtime_wait_note(3,elapsed);
    }
    return result;
}
static ABI int32_t kernel_usleep(uint32_t usec) { int e=sleep_ns((uint64_t)usec*1000); return e ? ERR(runtime_guest_errno(e)) : 0; }
static ABI int32_t posix_usleep(uint32_t usec) { int e=sleep_ns((uint64_t)usec*1000); return e ? fail_posix(e) : 0; }
static ABI uint32_t posix_sleep(uint32_t seconds) {
    int e=sleep_ns((uint64_t)seconds*1000000000);
    if (e) { *runtime_errno()=runtime_guest_errno(e); return seconds; }
    return 0;
}
static ABI int32_t posix_nanosleep(const GuestTimespec *rq,GuestTimespec *rem) {
    if (!rq || rq->nsec<0 || rq->nsec>=1000000000 || rq->sec<0) return fail_posix(EINVAL);
    if ((uint64_t)rq->sec>(UINT64_MAX-(uint64_t)rq->nsec)/1000000000) return fail_posix(EINVAL);
    int e=sleep_ns((uint64_t)rq->sec*1000000000+(uint64_t)rq->nsec);
    if (e) return fail_posix(e);
    if (rem) { rem->sec=0; rem->nsec=0; }
    return 0;
}
static ABI int32_t kernel_nanosleep(const GuestTimespec *rq,GuestTimespec *rem) {
    return posix_nanosleep(rq,rem) ? ERR(*runtime_errno()) : 0;
}
typedef struct { int32_t minuteswest, dsttime; } GuestTimezone;
static ABI int32_t kernel_gettimezone(GuestTimezone *tz) {
    if (!tz) return ERR(22);
#ifdef _WIN32
    TIME_ZONE_INFORMATION info;
    DWORD state=GetTimeZoneInformation(&info);
    if (state==TIME_ZONE_ID_INVALID) return ERR(5);
    tz->minuteswest=info.Bias+(state==TIME_ZONE_ID_DAYLIGHT ? info.DaylightBias : info.StandardBias);
    tz->dsttime=state==TIME_ZONE_ID_DAYLIGHT;
#else
    time_t now=time(NULL); struct tm local; localtime_r(&now,&local);
    tz->minuteswest=(int32_t)(-local.tm_gmtoff/60); tz->dsttime=local.tm_isdst>0;
#endif
    return 0;
}
static ABI int32_t posix_gettimeofday(GuestTimeval *tv,GuestTimezone *tz) {
    GuestTimespec t;
    int e=clock_read(0,&t);
    if (e) return fail_posix(e);
    if (tv) { tv->sec=t.sec; tv->usec=t.nsec/1000; }
    if (tz && kernel_gettimezone(tz)) return fail_posix(EIO);
    return 0;
}
static ABI int32_t kernel_gettimeofday(GuestTimeval *tv) {
    if (!tv) return ERR(22);
    return posix_gettimeofday(tv,NULL);
}
static ABI int64_t posix_time(int64_t *out) { int64_t t=(int64_t)time(NULL); if (out) *out=t; return t; }

/* ---- process ---- */
static ABI int32_t get_pagesize(void) { return PAGE; }
static ABI int32_t get_pid(void) { return 1000; }
static ABI int32_t yield(void) { sched_yield(); return 0; }
static ABI __attribute__((noreturn)) void hard_exit(int status) {
    printf("Runtime: guest requested _exit(%d)\n",status);
    runtime_report();
    fflush(stdout);
    _exit(status);
}
static ABI __attribute__((noreturn)) void raise_exception(uint32_t code,uint64_t argument) {
    fprintf(stderr,"STOP: guest raised debug exception code=0x%x argument=0x%llx\n",code,(unsigned long long)argument);
    runtime_report();
    exit(23);
}
static ABI int32_t print_backtrace(void) {
    fputs("Runtime: guest requested a backtrace (not available)\n",stderr);
    return 0;
}
typedef struct { uint32_t bits[4]; } GuestSigset;
static _Thread_local GuestSigset signal_mask;
static uintptr_t handlers[128];
static ABI uintptr_t guest_signal(int sig,uintptr_t handler) {
    if (sig<=0 || sig>=128) { *runtime_errno()=22; return (uintptr_t)-1; } /* SIG_ERR */
    uintptr_t old=handlers[sig]; handlers[sig]=handler;
    printf("Runtime: guest signal(%d) handler recorded (host signals are not forwarded)\n",sig);
    return old;
}
static ABI int32_t guest_sigprocmask(int how,const GuestSigset *set,GuestSigset *old) {
    if (old) *old=signal_mask;
    if (!set) return 0;
    for (int i=0;i<4;++i) {
        if (how==1) signal_mask.bits[i]|=set->bits[i];        /* SIG_BLOCK */
        else if (how==2) signal_mask.bits[i]&=~set->bits[i];  /* SIG_UNBLOCK */
        else if (how==3) signal_mask.bits[i]=set->bits[i];    /* SIG_SETMASK */
        else return fail_posix(EINVAL);
    }
    return 0;
}
static ABI int32_t guest_sigfillset(GuestSigset *set) { if (!set) return fail_posix(EINVAL); memset(set,0xff,sizeof(*set)); return 0; }
static ABI int32_t guest_sigemptyset(GuestSigset *set) { if (!set) return fail_posix(EINVAL); memset(set,0,sizeof(*set)); return 0; }
typedef struct { GuestTimeval utime, stime; int64_t rest[14]; } GuestRusage;
static ABI int32_t guest_getrusage(int who,GuestRusage *out) {
    if (!out || (who!=0 && who!=1)) return fail_posix(EINVAL);
    memset(out,0,sizeof(*out));
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    BOOL ok=who==0 ? GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user) :
                    GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user);
    if (!ok) return fail_posix(EIO);
    uint64_t u=((uint64_t)user.dwHighDateTime<<32)|user.dwLowDateTime;
    uint64_t k=((uint64_t)kernel.dwHighDateTime<<32)|kernel.dwLowDateTime;
    out->utime=(GuestTimeval){(int64_t)(u/10000000),(int64_t)(u%10000000/10)};
    out->stime=(GuestTimeval){(int64_t)(k/10000000),(int64_t)(k%10000000/10)};
#else
    struct rusage r;
    if (getrusage(who==0 ? RUSAGE_SELF : RUSAGE_THREAD,&r)) return fail_posix(errno);
    out->utime=(GuestTimeval){r.ru_utime.tv_sec,r.ru_utime.tv_usec};
    out->stime=(GuestTimeval){r.ru_stime.tv_sec,r.ru_stime.tv_usec};
#endif
    return 0;
}
static ABI int32_t guest_sysctl(const int32_t *name,uint32_t namelen,void *old,uint64_t *oldlen,const void *new_value,uint64_t newlen) {
    (void)newlen;
    if (!name || namelen<2 || new_value) return fail_posix(EINVAL);
    if (name[0]==1 && name[1]==37) { /* kern.arandom */
        if (!old || !oldlen) return fail_posix(EINVAL);
#ifdef _WIN32
        unsigned char *p=old;
        for (uint64_t at=0;at<*oldlen;) {
            unsigned int value;
            if (rand_s(&value)) return fail_posix(EIO);
            size_t n=*oldlen-at<sizeof(value) ? (size_t)(*oldlen-at) : sizeof(value);
            memcpy(p+at,&value,n); at+=n;
        }
#else
        size_t at=0;
        while (at<*oldlen) {
            ssize_t n=getrandom((char *)old+at,(size_t)(*oldlen-at),0);
            if (n<0) { if (errno==EINTR) continue; return fail_posix(errno); }
            if (!n) return fail_posix(EIO);
            at+=(size_t)n;
        }
#endif
        return 0;
    }
    if (name[0]==6 && (name[1]==7 || name[1]==3)) { /* hw.pagesize / hw.ncpu */
        if (!oldlen) return fail_posix(EINVAL);
        int32_t value=name[1]==7 ? PAGE : 7;
        if (old) { if (*oldlen<4) return fail_posix(ENOMEM); memcpy(old,&value,4); }
        *oldlen=4; return 0;
    }
    fprintf(stderr,"STOP: unsupported sysctl mib");
    for (uint32_t i=0;i<namelen && i<8;++i) fprintf(stderr," %d",name[i]);
    fputc('\n',stderr);
    exit(21);
}

static const RuntimeExport exports[]={
    {"sceKernelClockGettime",kernel_clock_gettime}, {"clock_gettime",posix_clock_gettime},
    {"clock_getres",posix_clock_getres},
    {"sceKernelGetProcessTime",process_time}, {"sceKernelGetProcessTimeCounter",process_time_counter},
    {"sceKernelGetProcessTimeCounterFrequency",process_time_frequency},
    {"sceKernelReadTsc",read_tsc}, {"sceKernelGetTscFrequency",tsc_frequency},
    {"sceKernelUsleep",kernel_usleep}, {"usleep",posix_usleep}, {"sleep",posix_sleep},
    {"nanosleep",posix_nanosleep}, {"sceKernelNanosleep",kernel_nanosleep},
    {"sceKernelGettimezone",kernel_gettimezone}, {"gettimeofday",posix_gettimeofday},
    {"sceKernelGettimeofday",kernel_gettimeofday},
    {"time",posix_time},
    {"getpagesize",get_pagesize}, {"getpid",get_pid}, {"sched_yield",yield},
    {"_exit",hard_exit},
    {"sceKernelDebugRaiseException",raise_exception},
    {"sceKernelDebugRaiseExceptionOnReleaseMode",raise_exception},
    {"sceKernelPrintBacktraceWithModuleInfo",print_backtrace},
    {"signal",guest_signal}, {"sigprocmask",guest_sigprocmask}, {"_sigprocmask",guest_sigprocmask},
    {"sigfillset",guest_sigfillset}, {"sigemptyset",guest_sigemptyset},
    {"getrusage",guest_getrusage}, {"sysctl",guest_sysctl},
};
uintptr_t runtime_kernel_resolve(const char *name) {
    uintptr_t thread=runtime_thread_keys_resolve(name);
    return thread ? thread : RUNTIME_LOOKUP(exports,name);
}
