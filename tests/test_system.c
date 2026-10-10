/* Exercise actual resolved SysV imports, including guest layouts and errno. */
#include "runtime.h"
#include "runtime_clock.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const struct { const char *nid,*symbol; } names[]={
#include "import_names.inc"
#include "runtime_contract_names.inc"
};
static uintptr_t resolve(const char *symbol) {
    for (size_t i=0;i<sizeof(names)/sizeof(*names);++i) if (!strcmp(names[i].symbol,symbol)) {
        uintptr_t p=runtime_resolve(names[i].nid,0); if (p) return p;
    }
    fprintf(stderr,"Missing export %s\n",symbol); assert(0); return 0;
}
#define GET(type,name) ((type)resolve(name))
typedef struct { int64_t sec,sub; } Time;
typedef int32_t (ABI *Clock)(uint32_t,Time *);
typedef uint64_t (ABI *Counter)(void);
typedef int32_t (ABI *Sleep)(const Time *,Time *);
typedef int32_t (ABI *Rusage)(int,void *);
typedef int32_t (ABI *Sysctl)(const int32_t *,uint32_t,void *,uint64_t *,const void *,uint64_t);
typedef int32_t (ABI *IntOut)(int32_t *);
typedef int32_t (ABI *NetState)(int32_t *);
typedef struct { uint16_t y,m,d,h,min,sec; uint32_t us; } Date;
typedef int32_t (ABI *DateToTick)(const Date *,uint64_t *);
typedef int32_t (ABI *TickToDate)(Date *,const uint64_t *);
int main(void) {
    runtime_start(1); runtime_thread_attach_main();
    Clock kernel=GET(Clock,"sceKernelClockGettime"),posix=GET(Clock,"clock_gettime");
    Time a,b;
    const uint32_t clocks[]={0,2,4,5,7,8,9,10,11,12,13,14,15,16,17,18,19};
    for (size_t i=0;i<sizeof(clocks)/sizeof(*clocks);++i) {
        assert(!kernel(clocks[i],&a)); assert(a.sec>=0 && a.sub>=0 && a.sub<1000000000);
        assert(!GET(Clock,"clock_getres")(clocks[i],&b)); assert(b.sub>=0 && b.sub<1000000000);
    }
    assert((uint32_t)kernel(999,&a)==0x80020016); assert(posix(999,&a)==-1 && *runtime_errno()==22);
    Counter us=GET(Counter,"sceKernelGetProcessTime"),ns=GET(Counter,"sceKernelGetProcessTimeCounter");
    uint64_t before=ns(),at=runtime_monotonic_ns();
    Time request={0,2000000},remaining={1,1};
    assert(!GET(Sleep,"nanosleep")(&request,&remaining)); assert(!remaining.sec && !remaining.sub);
    assert(runtime_monotonic_ns()-at>=2000000 && ns()>before && us()>0);
    request=(Time){INT64_MAX,999999999}; assert(GET(Sleep,"nanosleep")(&request,NULL)==-1 && *runtime_errno()==22);
    assert(GET(Counter,"sceKernelGetProcessTimeCounterFrequency")()==1000000000);
    assert(GET(Counter,"sceKernelGetTscFrequency")()>1000000);
    struct { Time u,s; int64_t rest[14]; uint64_t guard; } usage;
    memset(&usage,0xa5,sizeof(usage)); uint64_t guard=usage.guard;
    assert(!GET(Rusage,"getrusage")(0,&usage)); assert(usage.guard==guard && usage.u.sub<1000000);
    assert(!GET(Rusage,"getrusage")(1,&usage)); assert(GET(Rusage,"getrusage")(2,&usage)==-1);
    Sysctl sysctl=GET(Sysctl,"sysctl"); int32_t mib[]={6,7},value=0; uint64_t length=4;
    assert(!sysctl(mib,2,&value,&length,NULL,0) && value==16384 && length==4);
    length=3; assert(sysctl(mib,2,&value,&length,NULL,0)==-1 && *runtime_errno()==12);
    unsigned char random[35]={0}; mib[0]=1; mib[1]=37; length=sizeof(random);
    assert(!sysctl(mib,2,random,&length,NULL,0)); unsigned sum=0;
    for (size_t i=0;i<sizeof(random);++i) sum|=random[i]; assert(sum);
    assert(runtime_guest_errno(EAGAIN)==35 && runtime_guest_errno(ENOSYS)==78 && runtime_guest_errno(EACCES)==13);
    int32_t user=-1,state=-1;
    assert(!GET(IntOut,"sceUserServiceGetInitialUser")(&user) && user==1);
    assert(!GET(NetState,"sceNetCtlGetState")(&state) && state==0);
    Date leap={2024,2,29,23,59,58,123456},back={0}; uint64_t tick=0;
    assert(!GET(DateToTick,"sceRtcGetTick")(&leap,&tick));
    assert(!GET(TickToDate,"sceRtcSetTick")(&back,&tick) && !memcmp(&leap,&back,sizeof(leap)));
    leap.y=2023; assert(GET(DateToTick,"sceRtcGetTick")(&leap,&tick)!=0);
    puts("System: clocks, sleep, CPU time, errno, random, offline services and RTC PASS");
    return 0;
}
