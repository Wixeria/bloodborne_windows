/* Exercise production exports through the PS4 SysV ABI on Windows and Linux.
 * Uses real host threads; no renderer or game files are required. */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_clock.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef int32_t (ABI *AttrInit)(void **);
typedef int32_t (ABI *AttrType)(void **, int);
typedef int32_t (ABI *Init)(void **, void **, const char *);
typedef int32_t (ABI *Op)(void **);
typedef int32_t (ABI *TimedMutex)(void **, uint32_t);
typedef int32_t (ABI *CondWait)(void **, void **);
typedef int32_t (ABI *CondTimedWait)(void **, void **, uint32_t);
typedef struct { int64_t seconds, nanoseconds; } GuestTime;
typedef int32_t (ABI *AbsCondWait)(void **, void **, const GuestTime *);
typedef int32_t (ABI *TimedRw)(void **, const GuestTime *);
#define MUTEX(type, name) ((type)runtime_mutex_resolve(name))
#define RW(type, name) ((type)runtime_rwlock_resolve(name))
#define ERR(n) (UINT32_C(0x80020000) | (n))

static Op mutex_lock, mutex_unlock, mutex_trylock, mutex_destroy;
static Op rw_read, rw_write, rw_tryread, rw_trywrite, rw_unlock, rw_destroy;
static CondWait cond_wait;
static Op cond_signal, cond_broadcast;
static void setup(void) {
    mutex_lock=MUTEX(Op,"9UK1vLZQft4#p#J");
    mutex_unlock=MUTEX(Op,"tn3VlD0hG60#p#J");
    mutex_trylock=MUTEX(Op,"upoVrzMHFeE#p#J");
    mutex_destroy=MUTEX(Op,"2Of0f+3mhhE#p#J");
    rw_read=RW(Op,"Ox9i0c7L5w0#p#J"); rw_write=RW(Op,"mqdNorrB+gI#p#J");
    rw_tryread=RW(Op,"XD3mDeybCnk#p#J"); rw_trywrite=RW(Op,"bIHoZCTomsI#p#J");
    rw_unlock=RW(Op,"+L98PIbGttk#p#J"); rw_destroy=RW(Op,"BB+kb08Tl9A#p#J");
    cond_wait=MUTEX(CondWait,"WKAXJ4XBPQ4#p#J");
    cond_signal=MUTEX(Op,"kDh-NfxgMtE#p#J"); cond_broadcast=MUTEX(Op,"JGgj7Uvrl+A#p#J");
    assert(mutex_lock && mutex_unlock && mutex_trylock && mutex_destroy);
    assert(rw_read && rw_write && rw_tryread && rw_trywrite && rw_unlock && rw_destroy);
    assert(cond_wait && cond_signal && cond_broadcast);
    assert(!runtime_mutex_resolve("9UK1vLZQft4#q#q"));
    assert(!runtime_rwlock_resolve("Ox9i0c7L5w0#I#J"));
}

static uint64_t monotonic_ns(void) {
    struct timespec t; assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static void pause_ms(long ms) {
    struct timespec t={ms/1000,(ms%1000)*1000000};
    assert(nanosleep(&t,NULL)==0);
}
static GuestTime deadline_ms(long ms) {
    struct timespec t; assert(clock_gettime(CLOCK_REALTIME,&t)==0);
    int64_t ns=t.tv_nsec+(ms%1000)*1000000;
    return (GuestTime){(int64_t)t.tv_sec+ms/1000+ns/1000000000,ns%1000000000};
}

static void mutex_contracts(void) {
    AttrInit ai=MUTEX(AttrInit,"F8bUHwAG284#p#J");
    AttrType at=MUTEX(AttrType,"iMp8QpE+XO4#p#J");
    Op ad=MUTEX(Op,"smWEktiyyG0#p#J");
    Init init=MUTEX(Init,"cmo1RIYva9o#p#J");
    assert(ai && at && ad && init);
    void *attr=NULL,*m=NULL;
    assert((uint32_t)ai(NULL)==ERR(22));
    assert(ai(&attr)==0 && (uint32_t)at(&attr,99)==ERR(22));
    assert(at(&attr,2)==0 && init(&m,&attr,"recursive")==0 && ad(&attr)==0);
    assert(!attr && mutex_lock(&m)==0 && mutex_trylock(&m)==0);
    assert((uint32_t)mutex_destroy(&m)==ERR(16));
    assert(mutex_unlock(&m)==0 && mutex_unlock(&m)==0 && mutex_destroy(&m)==0);
    assert((uint32_t)mutex_lock(&m)==ERR(22));
    m=NULL;
    assert(mutex_lock(&m)==0); /* static initialization */
    assert((uint32_t)mutex_lock(&m)==ERR(11));
    assert((uint32_t)mutex_trylock(&m)==ERR(16));
    assert(mutex_unlock(&m)==0 && mutex_destroy(&m)==0);
    /* POSIX imports report positive guest errno, not encoded SCE errors. */
    m=NULL;
    Op pl=MUTEX(Op,"7H0iTOciTLo#I#J"), pu=MUTEX(Op,"2Z+PpY6CaJg#I#J");
    Op pd=MUTEX(Op,"ltCfaGr2JGE#I#J");
    assert(pl && pu && pd && pl(&m)==0 && pl(&m)==11 && pu(&m)==0 && pd(&m)==0);
}

typedef struct { void **mutex; int32_t result; } MutexTask;
static void *foreign_unlock(void *arg) {
    MutexTask *t=arg; t->result=mutex_unlock(t->mutex); return NULL;
}
static void *timed_mutex(void *arg) {
    MutexTask *t=arg;
    TimedMutex timed=MUTEX(TimedMutex,"IafI2PxcPnQ#p#J"); assert(timed);
    uint64_t before=monotonic_ns(); t->result=timed(t->mutex,20000);
    assert(monotonic_ns()-before>=15000000); return NULL;
}
static void mutex_contention(void) {
    void *m=NULL; assert(mutex_lock(&m)==0);
    MutexTask t={&m,0}; pthread_t worker;
    assert(pthread_create(&worker,NULL,foreign_unlock,&t)==0 && pthread_join(worker,NULL)==0);
    assert((uint32_t)t.result==ERR(1));
    assert(pthread_create(&worker,NULL,timed_mutex,&t)==0 && pthread_join(worker,NULL)==0);
    assert((uint32_t)t.result==ERR(60));
    assert(mutex_unlock(&m)==0 && mutex_destroy(&m)==0);
}

typedef struct { void *mutex, *cond; int ready, go; } CondTask;
static void *cond_worker(void *arg) {
    CondTask *t=arg;
    assert(mutex_lock(&t->mutex)==0); ++t->ready;
    assert(cond_broadcast(&t->cond)==0);
    while (!t->go) assert(cond_wait(&t->cond,&t->mutex)==0);
    assert(mutex_unlock(&t->mutex)==0); return NULL;
}
static void condition_contracts(void) {
    CondTask t={0}; pthread_t a,b;
    assert(pthread_create(&a,NULL,cond_worker,&t)==0);
    assert(pthread_create(&b,NULL,cond_worker,&t)==0);
    assert(mutex_lock(&t.mutex)==0);
    while (t.ready<2) assert(cond_wait(&t.cond,&t.mutex)==0);
    t.go=1; assert(cond_broadcast(&t.cond)==0 && mutex_unlock(&t.mutex)==0);
    assert(pthread_join(a,NULL)==0 && pthread_join(b,NULL)==0);
    CondTimedWait timed=MUTEX(CondTimedWait,"BmMjYxmew1w#p#J");
    AbsCondWait absolute=MUTEX(AbsCondWait,"27bAgiJmOh0#I#J");
    Op destroy=MUTEX(Op,"g+PZd2hiacg#p#J"); assert(timed && absolute && destroy);
    assert(mutex_lock(&t.mutex)==0);
    uint64_t before=monotonic_ns();
    assert((uint32_t)timed(&t.cond,&t.mutex,20000)==ERR(60));
    assert(monotonic_ns()-before>=15000000);
    GuestTime invalid={0,1000000000}, expired={0,0};
    assert(absolute(&t.cond,&t.mutex,&invalid)==22);
    assert(absolute(&t.cond,&t.mutex,&expired)==60);
    /* Every wait must return with the mutex reacquired. */
    assert((uint32_t)mutex_trylock(&t.mutex)==ERR(16));
    assert(mutex_unlock(&t.mutex)==0 && destroy(&t.cond)==0 && mutex_destroy(&t.mutex)==0);
}

typedef struct { void **rw; int32_t result; } RwTask;
static void *rw_foreign_unlock(void *arg) {
    RwTask *t=arg; t->result=rw_unlock(t->rw); return NULL;
}
static void *rw_timeout(void *arg) {
    RwTask *t=arg; TimedRw timed=RW(TimedRw,"adh--6nIqTk#p#J"); assert(timed);
    GuestTime end=deadline_ms(20); uint64_t before=monotonic_ns();
    t->result=timed(t->rw,&end); assert(monotonic_ns()-before>=15000000); return NULL;
}
static void rw_contracts(void) {
    void *r=NULL;
    assert(rw_read(&r)==0 && rw_read(&r)==0);
    assert((uint32_t)rw_write(&r)==ERR(11) && (uint32_t)rw_trywrite(&r)==ERR(16));
    assert((uint32_t)rw_destroy(&r)==ERR(16));
    RwTask t={&r,0}; pthread_t worker;
    assert(pthread_create(&worker,NULL,rw_foreign_unlock,&t)==0 && pthread_join(worker,NULL)==0);
    assert((uint32_t)t.result==ERR(1));
    assert(pthread_create(&worker,NULL,rw_timeout,&t)==0 && pthread_join(worker,NULL)==0);
    assert((uint32_t)t.result==ERR(60));
    assert(rw_unlock(&r)==0 && rw_unlock(&r)==0 && rw_write(&r)==0);
    assert((uint32_t)rw_read(&r)==ERR(11) && (uint32_t)rw_tryread(&r)==ERR(16));
    assert(rw_unlock(&r)==0 && rw_destroy(&r)==0);
    assert((uint32_t)rw_read(&r)==ERR(22));
}

typedef struct { void *rw; int value; _Atomic unsigned readers, writer; } Shared;
static void *write_worker(void *arg) {
    Shared *s=arg;
    for (int i=0;i<1000;++i) {
        assert(rw_write(&s->rw)==0);
        assert(atomic_fetch_add(&s->writer,1)==0 && atomic_load(&s->readers)==0);
        ++s->value; atomic_fetch_sub(&s->writer,1);
        assert(rw_unlock(&s->rw)==0);
    }
    return NULL;
}
static void *read_worker(void *arg) {
    Shared *s=arg;
    for (int i=0;i<1000;++i) {
        assert(rw_read(&s->rw)==0); atomic_fetch_add(&s->readers,1);
        assert(atomic_load(&s->writer)==0 && s->value>=0);
        atomic_fetch_sub(&s->readers,1); assert(rw_unlock(&s->rw)==0);
    }
    return NULL;
}
static void rw_concurrency(void) {
    Shared s={0}; pthread_t threads[4];
    for (int i=0;i<4;++i) assert(pthread_create(&threads[i],NULL,i<2 ? write_worker : read_worker,&s)==0);
    for (int i=0;i<4;++i) assert(pthread_join(threads[i],NULL)==0);
    assert(s.value==2000 && rw_destroy(&s.rw)==0);
}

static void wall_time(void) {
    typedef int (ABI *GetTime)(int64_t *,int32_t *);
    GetTime get=(GetTime)runtime_time_resolve("n88vx3C5nW8#I#J"); assert(get);
    struct { int64_t value[2]; uint64_t canary; } t={{0,0},0xabcdef};
    struct { int32_t value[2]; uint32_t canary; } z={{0,0},0x123456};
    time_t before=time(NULL); assert(get(t.value,z.value)==0); time_t after=time(NULL);
    assert(t.value[0]>=before && t.value[0]<=after && t.value[1]>=0 && t.value[1]<1000000);
    assert(t.canary==0xabcdef && z.canary==0x123456);
    assert(get(NULL,NULL)==0 && get(NULL,z.value)==0);
    assert(!runtime_time_resolve("n88vx3C5nW8#q#q"));
    /* Test the exact host clock path used by the audio runtime. */
    uint64_t start=runtime_monotonic_ns();
    assert(runtime_sleep_until(start+20000000)==0);
    assert(runtime_monotonic_ns()-start>=20000000);
    assert(runtime_sleep_until(start)==0);
    pause_ms(1);
}

static void *sleep_worker(void *unused) {
    (void)unused;
    /* Concurrent first use exercises key initialization; a second sleep uses
       the cached thread timer. Returning exercises its thread-exit destructor. */
    for (int i=0;i<2;++i) {
        uint64_t deadline=runtime_monotonic_ns()+5000000;
        assert(runtime_sleep_until(deadline)==0 && runtime_monotonic_ns()>=deadline);
    }
    return NULL;
}
static void parallel_clocks(void) {
    pthread_t workers[4];
    for (int i=0;i<4;++i) assert(pthread_create(&workers[i],NULL,sleep_worker,NULL)==0);
    for (int i=0;i<4;++i) assert(pthread_join(workers[i],NULL)==0);
}

int main(void) {
    setup(); mutex_contracts(); mutex_contention(); condition_contracts();
    rw_contracts(); rw_concurrency(); parallel_clocks(); wall_time();
    puts("PASS: SysV ABI, mutex ownership, condition waits, rwlock exclusion, timeouts and clocks");
    return 0;
}
