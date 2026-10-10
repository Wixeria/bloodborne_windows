/* Kernel semaphores: 32-bit IDs, counted tokens and FIFO waiters.
 * Priority-ordered semaphores are approximated by FIFO order. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef SRWLOCK SemaLock;
typedef CONDITION_VARIABLE SemaEvent;
#define SEMA_LOCK_INIT SRWLOCK_INIT
#else
typedef pthread_mutex_t SemaLock;
typedef pthread_cond_t SemaEvent;
#define SEMA_LOCK_INIT PTHREAD_MUTEX_INITIALIZER
#endif
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
typedef struct Waiter {
    int32_t need, result;
    int done;
    SemaEvent event;
    struct Waiter *next;
} Waiter;
typedef struct Sema {
    uint32_t id, attr;
    int32_t count, initial, maximum;
    unsigned active;
    int deleted;
    Waiter *first;
    struct Sema *next;
} Sema;
static SemaLock lock=SEMA_LOCK_INIT;
static Sema *registry;
static uint32_t next_id=1;
static size_t created, deleted, acquired, signaled, timed_out;
static Sema *find(uint32_t id) {
    for (Sema *s=registry;s;s=s->next) if (s->id==id) return s;
    return NULL;
}
static void host_check(int e) {
    if (e) { fprintf(stderr,"STOP: semaphore host error %d\n",e); exit(21); }
}
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) { perror("clock_gettime"); exit(1); }
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
/* Adapt the offline port's native SRW/condition-variable path for this
 * non-recursive registry only. Guest mutex ownership/error semantics are separate.
 * Waiter order, deletion and token accounting remain protected by the same lock. */
static void sema_lock(void) {
#ifdef _WIN32
    AcquireSRWLockExclusive(&lock);
#else
    pthread_mutex_lock(&lock);
#endif
}
static void sema_unlock(void) {
#ifdef _WIN32
    ReleaseSRWLockExclusive(&lock);
#else
    pthread_mutex_unlock(&lock);
#endif
}
static void event_init(SemaEvent *event) {
#ifdef _WIN32
    InitializeConditionVariable(event);
#else
    pthread_condattr_t attr;
    host_check(pthread_condattr_init(&attr));
    host_check(pthread_condattr_setclock(&attr,CLOCK_MONOTONIC));
    host_check(pthread_cond_init(event,&attr));
    host_check(pthread_condattr_destroy(&attr));
#endif
}
static void event_destroy(SemaEvent *event) {
#ifdef _WIN32
    (void)event; /* Native condition variables own no handle/allocation. */
#else
    host_check(pthread_cond_destroy(event));
#endif
}
static void event_signal(SemaEvent *event) {
#ifdef _WIN32
    WakeConditionVariable(event);
#else
    host_check(pthread_cond_signal(event));
#endif
}
static int event_wait(SemaEvent *event,uint64_t deadline) {
#ifdef _WIN32
    DWORD milliseconds=INFINITE;
    if (deadline) {
        uint64_t now=now_ns(), remaining=now>=deadline ? 0 : deadline-now;
        uint64_t ms=remaining/1000000+(remaining%1000000!=0);
        milliseconds=ms>=INFINITE ? INFINITE-1 : (DWORD)ms;
    }
    if (SleepConditionVariableSRW(event,&lock,milliseconds,0)) return 0;
    return GetLastError()==ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
#else
    if (!deadline) return pthread_cond_wait(event,&lock);
    struct timespec end={.tv_sec=(time_t)(deadline/1000000000),.tv_nsec=(long)(deadline%1000000000)};
    return pthread_cond_timedwait(event,&lock,&end);
#endif
}
static ABI int32_t sem_create(uint32_t *out,const char *name,uint32_t attr,
                              int32_t initial,int32_t maximum,const void *options) {
    if (!out || !name || attr>2 || initial<0 || maximum<=0 || initial>maximum) return ERR(22);
    if (options) { fputs("STOP: semaphore options are not implemented\n",stderr); exit(21); }
    Sema *s=calloc(1,sizeof(*s));
    if (!s) return ERR(12);
    sema_lock();
    if (next_id==UINT32_MAX) { sema_unlock(); free(s); return ERR(28); }
    s->id=next_id++; s->attr=attr; s->count=s->initial=initial; s->maximum=maximum;
    s->next=registry; registry=s; *out=s->id; ++created;
    printf("Runtime: semaphore id=%u, attr=%u, initial=%d, max=%d\n",s->id,attr,initial,maximum);
    sema_unlock();
    return 0;
}
static int32_t wait_count(uint32_t id,int32_t need,uint32_t *timeout,int block) {
    sema_lock();
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (need<=0 || need>s->maximum) result=ERR(22);
    else if (s->count>=need) { s->count-=need; ++acquired; }
    else if (!block) result=ERR(16);
    else if (timeout && !*timeout) { result=ERR(60); ++timed_out; }
    else {
        /* Priority-ordered semaphores (attr 2) are served FIFO: there is no
           PS4 priority scheduler, host threads run at equal priority. */
        if (getenv("BB_TRACE_SEMA")) fprintf(stderr,"Runtime: blocking wait on semaphore %u (need %d, count %d)\n",id,need,s->count); /* deadlock diagnosis */
        Waiter w={.need=need};
        event_init(&w.event);
        Waiter **tail=&s->first;
        while (*tail) tail=&(*tail)->next;
        *tail=&w; ++s->active;
        const uint64_t wait_start=runtime_wait_clock();
        uint64_t deadline=timeout ? now_ns()+(uint64_t)*timeout*1000 : 0;
        while (!w.done) {
            int e=event_wait(&w.event,deadline);
            if (e==ETIMEDOUT && timeout && now_ns()<deadline) continue;
            if (e==ETIMEDOUT && !w.done) {
                Waiter **p=&s->first;
                while (*p!=&w) p=&(*p)->next;
                *p=w.next; w.result=ERR(60); w.done=1; ++timed_out;
            } else if (e!=ETIMEDOUT) host_check(e);
        }
        runtime_wait_note(2,runtime_wait_clock()-wait_start);
        result=w.result;
        if (!result) ++acquired;
        if (timeout) {
            uint64_t now=now_ns();
            *timeout=result ? 0 : (uint32_t)(now>=deadline ? 0 : (deadline-now)/1000);
        }
        event_destroy(&w.event);
        if (!--s->active && s->deleted) free(s);
    }
    sema_unlock();
    return result;
}
static ABI int32_t sem_wait(uint32_t id,int32_t need,uint32_t *timeout) { return wait_count(id,need,timeout,1); }
static ABI int32_t sem_poll(uint32_t id,int32_t need) { return wait_count(id,need,NULL,0); }
static ABI int32_t sem_signal(uint32_t id,int32_t count) {
    sema_lock();
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (count<=0 || count>s->maximum-s->count) result=ERR(22);
    else {
        s->count+=count; ++signaled;
        Waiter **p=&s->first;
        while (*p) {
            Waiter *w=*p;
            if (w->need>s->count) { p=&w->next; continue; }
            s->count-=w->need; *p=w->next; w->done=1;
            event_signal(&w->event);
        }
    }
    sema_unlock();
    return result;
}
static int32_t wake_all(Sema *s,int32_t result) {
    int32_t count=0;
    while (s->first) {
        Waiter *w=s->first; s->first=w->next;
        w->result=result; w->done=1; ++count;
        event_signal(&w->event);
    }
    return count;
}
static ABI int32_t sem_cancel(uint32_t id,int32_t count,int32_t *waiters) {
    sema_lock();
    Sema *s=find(id);
    int32_t result=0;
    if (!s) result=ERR(3);
    else if (count>s->maximum) result=ERR(22);
    else {
        int32_t n=wake_all(s,ERR(85));
        if (waiters) *waiters=n;
        s->count=count<0 ? s->initial : count;
    }
    sema_unlock();
    return result;
}
static ABI int32_t sem_delete(uint32_t id) {
    sema_lock();
    Sema **p=&registry;
    while (*p && (*p)->id!=id) p=&(*p)->next;
    if (!*p) { sema_unlock(); return ERR(3); }
    Sema *s=*p; *p=s->next; s->deleted=1; ++deleted;
    wake_all(s,ERR(13));
    if (!s->active) free(s);
    sema_unlock();
    return 0;
}
/* Read-only test/diagnostic snapshot, not exposed to guest code. */
unsigned runtime_sema_waiters(uint32_t id) {
    sema_lock();
    Sema *s=find(id); unsigned n=0;
    if (s) for (Waiter *w=s->first;w;w=w->next) ++n;
    sema_unlock();
    return n;
}
uintptr_t runtime_sema_resolve(const char *name) {
    if (!strcmp(name,"188x57JYp0g#p#J")) return (uintptr_t)sem_create;
    if (!strcmp(name,"Zxa0VhQVTsk#p#J")) return (uintptr_t)sem_wait;
    if (!strcmp(name,"4czppHBiriw#p#J")) return (uintptr_t)sem_signal;
    if (!strcmp(name,"12wOHk8ywb0#p#J")) return (uintptr_t)sem_poll;
    if (!strcmp(name,"4DM06U2BNEY#p#J")) return (uintptr_t)sem_cancel;
    if (!strcmp(name,"R1Jvn8bSCW8#p#J")) return (uintptr_t)sem_delete;
    return 0;
}
void runtime_sema_report(void) {
    sema_lock();
    printf("Runtime: semaphores created=%zu, deleted=%zu, acquired=%zu, signals=%zu, timeouts=%zu\n",
           created,deleted,acquired,signaled,timed_out);
    sema_unlock();
}
