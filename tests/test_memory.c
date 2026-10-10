/* Production PS4 memory exports, with actual shared mappings on the host OS. */
#define _GNU_SOURCE
#include "runtime_memory.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define PAGE UINT64_C(16384)
#define ERR(n) (UINT32_C(0x80020000)|(n))
#define FIXED 0x10
#define NO_OVERWRITE 0x80
typedef uint64_t (ABI *Size)(void);
typedef int32_t (ABI *Allocate)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *);
typedef int32_t (ABI *Map)(void **,uint64_t,int,int,int64_t,uint64_t);
typedef int32_t (ABI *Release)(uint64_t,uint64_t);
typedef int32_t (ABI *Unmap)(void *,uint64_t);
typedef int32_t (ABI *Flexible)(void **,uint64_t,int,int);
typedef int32_t (ABI *Reserve)(void **,uint64_t,int,uint64_t);
typedef int32_t (ABI *Protect)(const void *,uint64_t,int);
typedef struct {
    uintptr_t start,end; uint64_t offset; int32_t protection,memory_type;
    uint32_t flags; char name[32];
} Query;
typedef int32_t (ABI *VirtualQueryGuest)(const void *,int,Query *,uint64_t);
typedef int32_t (ABI *QueryProtection)(void *,void **,void **,uint32_t *);
typedef struct { void *start; uint64_t offset,length; int8_t prot,type; int16_t reserved; int32_t operation; } Batch;
_Static_assert(sizeof(Query)==72,"guest virtual query layout");
_Static_assert(sizeof(Batch)==32,"guest batch map layout");
#define GET(type,nid) ((type)runtime_memory_resolve(nid))
static Size direct_size;
static Allocate allocate;
static Map map;
static Release release;
static Unmap unmap;
static Flexible flexible;
static Reserve reserve;
static Protect protect;
static VirtualQueryGuest query;
static QueryProtection query_protection;
static void setup(void) {
    direct_size=GET(Size,"pO96TwzOm5E#p#J");
    allocate=GET(Allocate,"rTXw65xmLIA#p#J"); map=GET(Map,"L-Q3LEjIbgA#p#J");
    release=GET(Release,"MBuItvba6z8#p#J"); unmap=GET(Unmap,"cQke9UuBQOk#p#J");
    flexible=GET(Flexible,"IWIBBdTHit4#p#J"); reserve=GET(Reserve,"7oxv3PPCumo#p#J");
    protect=GET(Protect,"vSMAm3cxYTY#p#J"); query=GET(VirtualQueryGuest,"rVjRvHJ0X6c#p#J");
    query_protection=GET(QueryProtection,"WFcfL2lzido#p#J");
    assert(direct_size && allocate && map && release && unmap && flexible && reserve && protect && query && query_protection);
    assert(direct_size()>=UINT64_C(5056)*1024*1024);
    assert(!runtime_memory_resolve("rTXw65xmLIA#q#q"));
}
static int64_t alloc_pages(uint64_t pages) {
    int64_t p=-1;
    assert(allocate(0,(int64_t)direct_size(),pages*PAGE,0,0,&p)==0 && p>=0);
    return p;
}
static unsigned char *map_pages(int64_t physical,uint64_t pages) {
    void *p=NULL;
    assert(map(&p,pages*PAGE,3,0,physical,0)==0 && p);
    assert((uintptr_t)p<UINT64_C(0x10000000000));
    return p;
}
static Query inspect(void *p) {
    struct { Query q; uint64_t canary; } value={{0},0x123456789abcdef};
    assert(query(p,0,&value.q,sizeof(value.q))==0 && value.canary==0x123456789abcdef);
    return value.q;
}
static void direct_aliases(void) {
    int64_t p=alloc_pages(3);
    assert(!runtime_memory_backing_pointer(direct_size()));
    unsigned char *a=map_pages(p,3),*b=map_pages(p+PAGE,1);
    unsigned char *backing=runtime_memory_backing_pointer((uint64_t)p);
    assert(backing);
    /* Both section offset and the second view's address may be 16 KiB aligned
       without being 64 KiB aligned. Contents must alias, never be copied. */
    assert((uint64_t)(p+PAGE)%65536==PAGE && (uintptr_t)b%65536==3*PAGE);
    a[PAGE]=17; assert(*b==17); *b=42; assert(a[PAGE]==42);
    assert(backing[PAGE]==42);
    runtime_memory_gpu_protect((uintptr_t)a,3*PAGE,1,0);
    backing[PAGE]=43; assert(a[PAGE]==43 && *b==43);
    runtime_memory_gpu_protect((uintptr_t)a,3*PAGE,1,1);
    *b=42;
    a[2*PAGE-1]=99; assert(b[PAGE-1]==99);
    assert(unmap(a,3*PAGE)==0 && *b==42);
    assert(release((uint64_t)p,3*PAGE)==0 && !runtime_memory_is_mapped((uintptr_t)b,PAGE));
    p=alloc_pages(3); a=map_pages(p,3);
    assert(a[PAGE]==0 && a[2*PAGE-1]==0);
    assert(release((uint64_t)p,3*PAGE)==0);
}
static void partial_direct(void) {
    int64_t p=alloc_pages(3);
    unsigned char *a=map_pages(p,3),*b=map_pages(p+2*PAGE,1);
    a[0]=10; a[PAGE]=20; a[2*PAGE]=30;
    /* Releasing a middle physical page removes only its aliases. */
    assert(release((uint64_t)p+PAGE,PAGE)==0);
    assert(a[0]==10 && a[2*PAGE]==30 && *b==30);
    assert(!runtime_memory_is_mapped((uintptr_t)a+PAGE,PAGE));
    assert(runtime_memory_clamp((uintptr_t)a,3*PAGE)==PAGE);
    Query q=inspect(a+2*PAGE); assert(q.offset==(uint64_t)p+2*PAGE);
    unsigned char byte=81;
    assert(runtime_memory_write_backing((uintptr_t)a+2*PAGE,&byte,1) && *b==81);
    assert(release((uint64_t)p,PAGE)==0 && release((uint64_t)p+2*PAGE,PAGE)==0);
    assert(!runtime_memory_is_mapped((uintptr_t)b,1));
}
static void flexible_splits(void) {
    void *raw=NULL; assert(flexible(&raw,3*PAGE,3,0)==0);
    unsigned char *a=raw; a[0]=10; a[PAGE]=20; a[2*PAGE]=30;
    assert(unmap(a+PAGE,PAGE)==0 && a[0]==10 && a[2*PAGE]==30);
    unsigned char byte=73;
    assert(runtime_memory_write_backing((uintptr_t)a+2*PAGE,&byte,1));
    assert(a[2*PAGE]==73 && a[0]==10);
    /* Reuse the freed physical page and virtual hole; it must be zeroed. */
    void *middle=a+PAGE;
    assert(flexible(&middle,PAGE,3,FIXED)==0 && middle==a+PAGE && a[PAGE]==0);
    a[PAGE]=99; assert(a[0]==10 && a[2*PAGE]==73);
    assert(runtime_memory_is_mapped((uintptr_t)a,3*PAGE));
    assert(unmap(a,3*PAGE)==0);
    raw=NULL; assert(flexible(&raw,3*PAGE,3,0)==0);
    a=raw; assert(a[0]==0 && a[PAGE]==0 && a[2*PAGE]==0);
    assert(unmap(a,3*PAGE)==0);
}
static void reservations_and_queries(void) {
    void *base=NULL; assert(reserve(&base,4*PAGE,0,0)==0);
    Query q=inspect(base); assert(!strcmp(q.name,"reserved") && q.flags==0);
    assert(!runtime_memory_is_mapped((uintptr_t)base,1));
    void *again=base;
    assert((uint32_t)reserve(&again,PAGE,FIXED,0)==ERR(12));
    int64_t p=alloc_pages(2); void *inside=(unsigned char *)base+PAGE;
    assert(map(&inside,2*PAGE,3,FIXED|NO_OVERWRITE,p,0)==0);
    assert(inside==(unsigned char *)base+PAGE);
    q=inspect(inside); assert(q.flags==18 && q.offset==(uint64_t)p && q.protection==3);
    assert((uint32_t)map(&inside,PAGE,3,FIXED|NO_OVERWRITE,p,0)==ERR(12));
    void *lo=NULL,*hi=NULL; uint32_t prot=0;
    assert(query_protection(inside,&lo,&hi,&prot)==0 && prot==3 && lo==inside && hi==(unsigned char *)inside+2*PAGE);
    assert(protect(inside,PAGE,1)==0);
    q=inspect(inside); assert(q.protection==1 && q.end==(uintptr_t)inside+PAGE);
    q=inspect((unsigned char *)inside+PAGE); assert(q.protection==3 && q.offset==(uint64_t)p+PAGE);
    assert((uint32_t)query(base,0,&q,sizeof(q)-1)==ERR(22));
    assert((uint32_t)query_protection(base,NULL,NULL,NULL)==ERR(13));
    assert(unmap(base,4*PAGE)==0 && release((uint64_t)p,2*PAGE)==0);
}
static void batch_operations(void) {
    /* BatchMap is present in the game's imports; its flags are fixed. */
    typedef int32_t (ABI *BatchFixed)(Batch *,int,int *);
    BatchFixed fixed=GET(BatchFixed,"2SKEx6bSq-4#p#J"); assert(fixed);
    void *base=NULL; assert(reserve(&base,3*PAGE,0,0)==0);
    Batch entries[]={
        {base,0,PAGE,3,0,0,3},
        {base,0,PAGE,1,0,0,2},
        {(unsigned char *)base+PAGE,0,PAGE,3,0,0,99}
    };
    struct { int count; uint32_t canary; } done={-1,0xabcdef};
    assert((uint32_t)fixed(entries,3,&done.count)==ERR(22));
    assert(done.count==2 && done.canary==0xabcdef && inspect(base).protection==1);
    assert(runtime_memory_is_mapped((uintptr_t)base,PAGE));
    assert(!runtime_memory_is_mapped((uintptr_t)base+PAGE,PAGE));
    entries[0]=(Batch){base,0,PAGE,0,0,0,1};
    assert(fixed(entries,1,&done.count)==0 && done.count==1);
    assert(unmap(base,3*PAGE)==0);
}
static int mapped_hooks,unmapped_hooks,invalidated_hooks;
static void on_map(uintptr_t address,uint64_t size) {
    ++mapped_hooks; assert(runtime_memory_is_mapped(address,size));
}
static void on_unmap(uintptr_t address,uint64_t size) {
    ++unmapped_hooks; (void)runtime_memory_clamp(address,size);
}
static void on_invalidate(uintptr_t address,uint64_t size) {
    ++invalidated_hooks; assert(runtime_memory_is_mapped(address,size));
}
#ifdef _WIN32
static volatile unsigned protection_faults;
__attribute__((naked,noinline)) static void probe_write(unsigned char *p __attribute__((unused))) {
    __asm__("movb $1, (%rcx)\n\tret");
}
__attribute__((naked,noinline)) static unsigned probe_read(unsigned char *p __attribute__((unused))) {
    __asm__("movzbl (%rcx), %eax\n\tret");
}
static LONG CALLBACK expected_fault(PEXCEPTION_POINTERS e) {
    if (e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    void *pc=e->ExceptionRecord->ExceptionAddress;
    if (pc!=(void *)probe_write && pc!=(void *)probe_read) return EXCEPTION_CONTINUE_SEARCH;
    ++protection_faults;
    e->ContextRecord->Rip+=3; /* exactly the single three-byte probe instruction */
    e->ContextRecord->Rax=0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void check_fault(unsigned char *address,int read,int expected) {
    protection_faults=0;
    if (read) (void)probe_read(address); else probe_write(address);
    assert(protection_faults==(unsigned)expected);
}
#endif
static void gpu_backing_and_protection(void) {
    runtime_memory_set_gpu_hooks(on_map,on_unmap,on_invalidate);
    int64_t p=alloc_pages(2); unsigned char *a=map_pages(p,2),*b=map_pages(p,2);
    assert(mapped_hooks==2);
    assert(protect(a,PAGE,1)==0 && invalidated_hooks==1);
#ifdef _WIN32
    void *handler=AddVectoredExceptionHandler(1,expected_fault); assert(handler);
    check_fault(a,0,1); check_fault(a,1,0);
#endif
    unsigned char byte=37;
    assert(runtime_memory_write_backing((uintptr_t)a,&byte,1) && *a==37 && *b==37);
    runtime_memory_gpu_protect((uintptr_t)a+4096,4096,0,0);
#ifdef _WIN32
    check_fault(a+4096,1,1); check_fault(a+8192,1,0);
#endif
    byte=83; assert(runtime_memory_write_backing((uintptr_t)a+4096,&byte,1) && b[4096]==83);
    /* GPU readback must use the backing alias even while the guest view has
       no access, so a copy worker cannot reenter a GPU fault it is waiting on. */
    unsigned char copied=0;
    runtime_memory_read_backing((uintptr_t)a+4096,&copied,1);
    assert(copied==83);
    runtime_memory_gpu_protect((uintptr_t)a+4096,4096,1,1);
    assert(a[4096]==83);
#ifdef _WIN32
    check_fault(a+4096,0,1); /* never grant writes beyond the guest's own RO permission */
    assert(RemoveVectoredExceptionHandler(handler));
#endif
    assert(protect(a,PAGE,3)==0);
    assert(release((uint64_t)p,2*PAGE)==0 && unmapped_hooks>=2);
    assert(!runtime_memory_write_backing((uintptr_t)a,&byte,1));
    copied=255;
    runtime_memory_read_backing((uintptr_t)a,&copied,1);
    assert(copied==0);
    runtime_memory_set_gpu_hooks(NULL,NULL,NULL);
}
static void bounds_and_replacement(void) {
    int64_t p=-1;
    assert((uint32_t)allocate(0,(int64_t)direct_size(),1,0,0,&p)==ERR(22) && p==-1);
    assert((uint32_t)allocate(0,PAGE,PAGE,3,0,&p)==ERR(22));
    p=alloc_pages(1); unsigned char *a=map_pages(p,1); *a=91;
    void *same=a;
    assert((uint32_t)map(&same,PAGE,3,FIXED|NO_OVERWRITE,p,0)==ERR(12) && *a==91);
    assert((uint32_t)map(&same,UINT64_MAX-PAGE+1,3,FIXED,p,0)==ERR(22) && *a==91);
    assert((uint32_t)unmap(a,UINT64_MAX)==ERR(22) && *a==91);
    assert((uint32_t)protect(a,UINT64_MAX,3)==ERR(22) && *a==91);
    assert((uint32_t)reserve(&same,UINT64_MAX-PAGE+1,FIXED,0)==ERR(22) && *a==91);
    void *hint=(void *)(uintptr_t)UINT64_MAX;
    assert((uint32_t)reserve(&hint,PAGE,0,0)==ERR(12) && (uintptr_t)hint==UINT64_MAX);
    assert((uint32_t)release((uint64_t)p,UINT64_MAX-PAGE+1)==ERR(22) && *a==91);
    assert((uint32_t)map(&same,PAGE,3,FIXED,p+PAGE,0)==ERR(22) && *a==91);
    assert((uint32_t)map(&same,PAGE,0x40,FIXED,p,0)==ERR(22) && *a==91);
    assert(flexible(&same,PAGE,3,FIXED)==0 && same==a && *a==0);
    *a=72;
    assert(release((uint64_t)p,PAGE)==0 && *a==72); /* replacing a direct VMA removed that alias */
    assert(unmap(a,PAGE)==0);
}
static void executable_memory(void) {
    /* x86-64 SysV: mov eax,edi; add eax,esi; ret. This also checks that a
       writable section alias can be made executable without copying code. */
    static const unsigned char code[]={0x89,0xf8,0x01,0xf0,0xc3};
    typedef int (ABI *GuestAdd)(int,int);
    int64_t physical=alloc_pages(1);
    unsigned char *a=map_pages(physical,1),*alias=map_pages(physical,1);
    memcpy(alias,code,sizeof(code));
    assert(protect(a,PAGE,BB_MEMORY_READ|BB_MEMORY_EXECUTE)==0);
    assert(((GuestAdd)(uintptr_t)a)(17,25)==42);
    unsigned char *low=runtime_low_map(PAGE,BB_MEMORY_READ|BB_MEMORY_WRITE|BB_MEMORY_EXECUTE);
    assert(low); memcpy(low,code,sizeof(code));
#ifdef _WIN32
    assert(FlushInstructionCache(GetCurrentProcess(),low,sizeof(code)));
#endif
    assert(((GuestAdd)(uintptr_t)low)(29,13)==42);
#ifdef _WIN32
    void *handler=AddVectoredExceptionHandler(1,expected_fault); assert(handler);
    check_fault(a,0,1); check_fault(alias,0,0);
    assert(RemoveVectoredExceptionHandler(handler));
#endif
    assert(release((uint64_t)physical,PAGE)==0);
}
static void *memory_worker(void *unused) {
    (void)unused;
    for (int i=0;i<20;++i) {
        int64_t p=alloc_pages(2); unsigned char *a=map_pages(p,2),*b=map_pages(p+PAGE,1);
        a[PAGE]=(unsigned char)(i+1);
        if (*b!=(unsigned char)(i+1)) {
            Query qa=inspect(a+PAGE),qb=inspect(b);
            fprintf(stderr,"alias mismatch: loop=%d physical=0x%llx a=%p b=%p values=%u/%u offsets=0x%llx/0x%llx\n",
                    i,(unsigned long long)p,(void *)a,(void *)b,a[PAGE],*b,
                    (unsigned long long)qa.offset,(unsigned long long)qb.offset);
            _Exit(1);
        }
        if (!runtime_memory_is_mapped((uintptr_t)a,2*PAGE)) {
            fprintf(stderr,"mapping disappeared: loop=%d physical=0x%llx a=%p b=%p\n",
                    i,(unsigned long long)p,(void *)a,(void *)b);
            _Exit(1);
        }
        assert(release((uint64_t)p,2*PAGE)==0);
    }
    return NULL;
}
static void concurrent_memory(void) {
    pthread_t workers[4];
    for (int i=0;i<4;++i) assert(pthread_create(&workers[i],NULL,memory_worker,NULL)==0);
    for (int i=0;i<4;++i) assert(pthread_join(workers[i],NULL)==0);
}
static void low_memory(void) {
    unsigned char *a=runtime_low_map(PAGE,BB_MEMORY_READ|BB_MEMORY_WRITE);
    unsigned char *b=runtime_low_map(PAGE,BB_MEMORY_READ|BB_MEMORY_WRITE);
    assert(a && b && a!=b && (uintptr_t)a<UINT64_C(0x10000000000));
    *a=17; *b=29; assert(*a==17 && *b==29);
    assert(!runtime_low_map(0,3) && !runtime_low_map(SIZE_MAX,3));
#ifdef _WIN32
    void *handler=AddVectoredExceptionHandler(1,expected_fault); assert(handler);
    check_fault(a+PAGE,1,1); /* deliberate unmapped guard between allocations */
    assert(runtime_low_unmap(a,PAGE)==0);
    check_fault(a,1,1); /* private allocation was released back to placeholder */
    assert(RemoveVectoredExceptionHandler(handler));
#else
    assert(runtime_low_unmap(a,PAGE)==0);
#endif
    assert(runtime_low_unmap(b+1,PAGE)==-1 && *b==29);
    assert(runtime_low_unmap(b,PAGE)==0);
}
#ifdef _WIN32
static HANDLE nested_go,nested_attempt;
static unsigned char *nested_output;
static unsigned nested_faults;
static LONG CALLBACK nested_recovery(EXCEPTION_POINTERS *exception) {
    EXCEPTION_RECORD *record=exception->ExceptionRecord;
    uintptr_t fault=(uintptr_t)record->ExceptionInformation[1];
    if (record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION ||
        fault<(uintptr_t)nested_output || fault>=(uintptr_t)nested_output+PAGE)
        return EXCEPTION_CONTINUE_SEARCH;
    ++nested_faults;
    assert(SetEvent(nested_go));
    assert(WaitForSingleObject(nested_attempt,2000)==WAIT_OBJECT_0);
    Sleep(10); /* Give the writer time to queue behind the outer query reader. */
    runtime_memory_gpu_protect((uintptr_t)nested_output,PAGE,1,1);
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void *nested_writer(void *source) {
    assert(WaitForSingleObject(nested_go,2000)==WAIT_OBJECT_0);
    assert(SetEvent(nested_attempt));
    assert(protect(source,PAGE,1)==0);
    return NULL;
}
static void nested_fault_reader(void) {
    int64_t physical=alloc_pages(2);
    unsigned char *source=map_pages(physical,1);
    nested_output=map_pages(physical+PAGE,1);
    nested_go=CreateEventW(NULL,TRUE,FALSE,NULL);
    nested_attempt=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(nested_go && nested_attempt);
    runtime_memory_gpu_protect((uintptr_t)nested_output,PAGE,0,0);
    void *handler=AddVectoredExceptionHandler(1,nested_recovery); assert(handler);
    pthread_t writer; assert(pthread_create(&writer,NULL,nested_writer,source)==0);
    /* The query holds a reader lock when writing into GPU-protected output.
       Recovery reenters the same reader while a writer is waiting. */
    assert(query(source,0,(Query *)nested_output,sizeof(Query))==0);
    assert(pthread_join(writer,NULL)==0 && nested_faults==1);
    Query result=*(Query *)nested_output;
    assert(result.start==(uintptr_t)source && result.offset==(uint64_t)physical && result.protection==3);
    assert(inspect(source).protection==1);
    assert(RemoveVectoredExceptionHandler(handler));
    assert(CloseHandle(nested_go) && CloseHandle(nested_attempt));
    assert(release((uint64_t)physical,2*PAGE)==0);
}
static void foreign_host_memory(void) {
    /* Guest fixed maps must never clobber a host allocation that has no VMA. */
    MEM_ADDRESS_REQUIREMENTS requirements={
        (void *)(uintptr_t)UINT64_C(0x1000000000),
        (void *)(uintptr_t)(UINT64_C(0x1000000000)+64*1024*1024-1),65536
    };
    MEM_EXTENDED_PARAMETER parameter={0};
    parameter.Type=MemExtendedParameterAddressRequirements;
    parameter.Pointer=&requirements;
    unsigned char *foreign=VirtualAlloc2(NULL,NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,&parameter,1);
    assert(foreign); foreign[0]=91; foreign[65535]=72;
    int64_t physical=alloc_pages(1);
    void *fixed=foreign;
    assert((uint32_t)map(&fixed,PAGE,3,FIXED,physical,0)==ERR(12));
    assert(fixed==foreign && foreign[0]==91 && foreign[65535]==72);
    void *hint=foreign;
    assert(map(&hint,PAGE,3,0,physical,0)==0 && (uintptr_t)hint>=(uintptr_t)foreign+65536);
    *(unsigned char *)hint=37;
    assert(foreign[0]==91 && foreign[65535]==72);
    assert(release((uint64_t)physical,PAGE)==0);
    assert(VirtualFree(foreign,0,MEM_RELEASE));
}
static void placeholder_boundaries(void) {
    /* A private low mapping crosses two separately reserved arena chunks. */
    unsigned char *low=runtime_low_map(PAGE,3);
    const uint64_t span=UINT64_C(64)*1024*1024;
    unsigned char *large=runtime_low_map((size_t)span,3);
    assert(low && large && large==low+2*PAGE);
    *low=17; large[0]=29; large[span-1]=83;
    assert(*low==17 && large[0]==29 && large[span-1]==83);
    /* Guest reservations and shared views also straddle arena boundaries. */
    void *base=NULL; assert(reserve(&base,PAGE,0,0)==0);
    void *cross=(unsigned char *)base+span-2*PAGE;
    assert(reserve(&cross,4*PAGE,FIXED,0)==0);
    int64_t physical=alloc_pages(4);
    assert(map(&cross,4*PAGE,3,FIXED,physical,0)==0);
    unsigned char *a=cross; a[0]=11; a[4*PAGE-1]=44;
    assert(protect(a,4*PAGE,1)==0 && a[0]==11 && a[4*PAGE-1]==44);
    assert(release((uint64_t)physical,4*PAGE)==0 && unmap(base,PAGE)==0);
}
#endif
#ifdef _WIN32
static DWORD permissions(const void *p) {
    MEMORY_BASIC_INFORMATION info={0};
    assert(VirtualQuery(p,&info,sizeof(info))==sizeof(info));
    return info.Protect;
}
static void shared_traps(void) {
    int64_t physical=alloc_pages(2);
    unsigned char *p=map_pages(physical,2);
    runtime_memory_trap((uintptr_t)p,2*PAGE,1,1);
    runtime_memory_trap((uintptr_t)p,2*PAGE,2,1);
    assert(runtime_memory_trap_reasons((uintptr_t)p)==3);
    assert(permissions(p)==PAGE_READONLY && permissions(p+2*PAGE-1)==PAGE_READONLY);
    runtime_memory_trap((uintptr_t)p,2*PAGE,1,0);
    assert(runtime_memory_trap_reasons((uintptr_t)p)==2 && permissions(p)==PAGE_READONLY);
    runtime_memory_trap((uintptr_t)p,4096,32,1);
    assert(permissions(p)==PAGE_NOACCESS && permissions(p+4096)==PAGE_READONLY);
    runtime_memory_trap((uintptr_t)p,4096,32,0);
    assert(permissions(p)==PAGE_READONLY);
    assert(!protect(p,2*PAGE,3));
    assert(permissions(p)==PAGE_READONLY && permissions(p+2*PAGE-1)==PAGE_READONLY);
    runtime_memory_trap((uintptr_t)p,2*PAGE,2,0);
    assert(permissions(p)==PAGE_READWRITE && permissions(p+2*PAGE-1)==PAGE_READWRITE);
    p[0]=31; p[2*PAGE-1]=91;
    runtime_memory_trap((uintptr_t)p,2*PAGE,1,1);
    assert(!unmap(p,2*PAGE));
    assert(!runtime_memory_trap_reasons((uintptr_t)p));
    void *fixed=p;
    assert(!map(&fixed,2*PAGE,3,FIXED,physical,0) && fixed==p);
    assert(permissions(p)==PAGE_READWRITE && p[0]==31 && p[2*PAGE-1]==91);
    assert(!release(physical,2*PAGE));
}
static unsigned chunk_calls;
static int chunk_import(uint64_t physical,uint64_t size) {
    const uint64_t chunk=UINT64_C(64)<<20;
    assert(size==chunk && physical%chunk==0);
    unsigned char *backing=runtime_memory_backing_pointer(physical);
    assert(backing && permissions(backing)==PAGE_READWRITE);
    assert(permissions(backing+size-1)==PAGE_READWRITE);
    backing[0]=17; backing[size-1]=29;
    ++chunk_calls; return -3;
}
static void imported_chunks(void) {
    runtime_memory_set_guest_chunk_allocator(chunk_import);
    int64_t a=alloc_pages(1),b=alloc_pages(1);
    assert(a==0 && b==PAGE && chunk_calls==1);
    unsigned char *p=map_pages(a,1);
    assert(p[0]==17);
    p[1]=37; assert(((unsigned char *)runtime_memory_backing_pointer(a))[1]==37);
    const uint64_t chunk=UINT64_C(64)<<20;
    int64_t c=-1;
    assert(!allocate(chunk,direct_size(),PAGE,0,0,&c) && (uint64_t)c==chunk && chunk_calls==2);
    assert(!release(a,PAGE) && !release(b,PAGE) && !release(c,PAGE));
    assert(alloc_pages(1)==a && chunk_calls==2); /* Imported pool ranges keep their handles. */
    assert(!release(a,PAGE));
    runtime_memory_set_guest_chunk_allocator(NULL);
}
#endif
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    setup(); assert(argc==2);
    if (!strcmp(argv[1],"direct")) direct_aliases();
    else if (!strcmp(argv[1],"partial-direct")) partial_direct();
    else if (!strcmp(argv[1],"flexible")) flexible_splits();
    else if (!strcmp(argv[1],"regions")) reservations_and_queries();
    else if (!strcmp(argv[1],"batch")) batch_operations();
    else if (!strcmp(argv[1],"gpu")) gpu_backing_and_protection();
    else if (!strcmp(argv[1],"bounds")) bounds_and_replacement();
    else if (!strcmp(argv[1],"execute")) executable_memory();
    else if (!strcmp(argv[1],"concurrency")) concurrent_memory();
    else if (!strcmp(argv[1],"low")) low_memory();
#ifdef _WIN32
    else if (!strcmp(argv[1],"traps")) shared_traps();
    else if (!strcmp(argv[1],"chunks")) imported_chunks();
    else if (!strcmp(argv[1],"nested-reader")) nested_fault_reader();
    else if (!strcmp(argv[1],"foreign")) foreign_host_memory();
    else if (!strcmp(argv[1],"boundaries")) placeholder_boundaries();
#endif
    else assert(!"unknown test mode");
    printf("PASS: memory %s\n",argv[1]); return 0;
}
