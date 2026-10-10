#define _GNU_SOURCE
#include "runtime_thread.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#define ERR(n) (UINT32_C(0x80020000)|(n))
typedef void *(ABI *Entry)(void *);
typedef int32_t (ABI *Create)(void **,void **,Entry,void *,const char *);
typedef int32_t (ABI *Join)(void *,void **);
typedef int32_t (ABI *Detach)(void *);
typedef void (ABI *Exit)(void *);
typedef void *(ABI *Self)(void);
typedef int32_t *(ABI *Error)(void);
typedef int32_t (ABI *AttrOp)(void **);
typedef int32_t (ABI *AttrSize)(void **,uint64_t);
typedef int32_t (ABI *AttrState)(void **,int);
typedef int32_t (ABI *KeyCreate)(uint32_t *,void (ABI *)(void *));
typedef int32_t (ABI *KeyDelete)(uint32_t);
typedef int32_t (ABI *KeySet)(uint32_t,void *);
typedef void *(ABI *KeyGet)(uint32_t);
typedef void *(ABI *Tls)(const uint64_t *);
typedef int32_t (ABI *Once)(int32_t *,void (ABI *)(void));
static Create create;
static Join join;
static Detach detach;
static Self self;
static Error error;
static AttrOp attr_init,attr_destroy;
static AttrSize set_stack;
static AttrState set_detach;
static KeyCreate key_create;
static KeyDelete key_delete;
static KeySet key_set;
static KeyGet key_get;
static Tls module_tls;
static Once once;
static Entry load_tcb;
#ifdef _WIN32
static _Thread_local uintptr_t native_marker;
#endif
static void *main_handle;
static const unsigned char initial[]={42,17,83};
static uintptr_t key_symbol(const char *symbol);
static void setup(void) {
#define THREAD(type,nid) ((type)runtime_thread_resolve(nid))
#define KEY(type,symbol) ((type)key_symbol(symbol))
    create=THREAD(Create,"6UgtwV+0zb4#p#J"); join=THREAD(Join,"onNY9Byn-W8#p#J");
    detach=THREAD(Detach,"4qGrR6eoP9Y#p#J"); self=THREAD(Self,"aI+OeCz8xrQ#p#J");
    error=THREAD(Error,"9BcDykPmo1I#p#J");
    attr_init=THREAD(AttrOp,"nsYoNRywwNg#p#J"); attr_destroy=THREAD(AttrOp,"62KCwEMmzcM#p#J");
    set_stack=THREAD(AttrSize,"UTXzJbWhhTE#p#J"); set_detach=THREAD(AttrState,"-Wreprtu0Qs#p#J");
    key_create=KEY(KeyCreate,"scePthreadKeyCreate"); key_delete=KEY(KeyDelete,"scePthreadKeyDelete");
    key_set=KEY(KeySet,"scePthreadSetspecific"); key_get=KEY(KeyGet,"scePthreadGetspecific");
    once=KEY(Once,"scePthreadOnce"); module_tls=(Tls)runtime_tls_resolve("vNe1w4diLCs#p#J");
    assert(create && join && detach && self && error && attr_init && attr_destroy && set_stack && set_detach);
    assert(key_create && key_delete && key_set && key_get && once && module_tls);
    assert(!runtime_thread_resolve("6UgtwV+0zb4#q#q") && !runtime_tls_resolve("vNe1w4diLCs#q#q"));
    runtime_set_main_tls(initial,sizeof(initial),64,16);
    runtime_set_libc_tls(initial,sizeof(initial),32);
    runtime_set_module_tls(3,NULL,0,48);
    static const unsigned char code[]={0x65,0x48,0x8b,0x04,0x25,0,0,0,0,0xc3};
    unsigned char *executable=runtime_low_map(sizeof(code),7); assert(executable);
    memcpy(executable,code,sizeof(code));
#ifdef _WIN32
    assert(runtime_thread_patch_tls_loads(executable,sizeof(code))==1);
    assert(runtime_thread_patch_tls_loads(executable,sizeof(code))==0);
#else
    assert(runtime_thread_patch_tls_loads(executable,sizeof(code))==0);
#endif
    load_tcb=(Entry)(uintptr_t)executable;
    runtime_thread_attach_main(); main_handle=self();
}
/* Resolve real NIDs through the generated import table, never a mock export. */
static uintptr_t key_symbol(const char *symbol) {
    static const struct { const char *nid,*symbol; } names[]={
#include "import_names.inc"
    };
    for (size_t i=0;i<sizeof(names)/sizeof(*names);++i)
        if (!strcmp(names[i].symbol,symbol)) {
            uintptr_t p=runtime_thread_keys_resolve(names[i].nid); if (p) return p;
        }
    assert(!"missing key symbol"); return 0;
}
static void verify_tls(int fresh) {
    uint64_t *tcb=load_tcb(NULL);
    assert(tcb==runtime_thread_tcb() && tcb[0]==(uintptr_t)tcb && tcb[2]==(uintptr_t)self());
    unsigned char *main_tls=(unsigned char *)tcb-64;
    if (fresh) assert(!memcmp(main_tls,initial,sizeof(initial)) && main_tls[63]==0);
    assert((uintptr_t)tcb<UINT64_C(0x10000000000));
    const uint64_t index[]={2,0},last[]={2,31},zero[]={3,0};
    unsigned char *module=module_tls(index),*bss=module_tls(zero);
    assert(module_tls(last)==module+31);
    if (fresh) assert(!memcmp(module,initial,sizeof(initial)) && module[31]==0 && bss[0]==0 && bss[47]==0);
    volatile unsigned char stack_marker=0;
    assert((uintptr_t)&stack_marker<UINT64_C(0x10000000000));
    assert(error()==runtime_errno());
#ifdef _WIN32
    /* The guest slot must coexist with compiler TLS, another Windows TLS slot,
       Win32 calls and winpthreads' own thread identity. */
    void *teb=NtCurrentTeb(); DWORD id=GetCurrentThreadId();
    DWORD slot=TlsAlloc(); assert(slot!=TLS_OUT_OF_INDEXES);
    assert(TlsSetValue(slot,(void *)(uintptr_t)0x12345678));
    native_marker=0x98765432;
    char *heap=malloc(80); assert(heap); memset(heap,17,80);
    assert(load_tcb(NULL)==tcb && TlsGetValue(slot)==(void *)(uintptr_t)0x12345678);
    assert(native_marker==0x98765432 && pthread_self() && NtCurrentTeb()==teb && GetCurrentThreadId()==id);
    assert(TlsFree(slot)); free(heap);
#endif
}
static ABI void *tls_worker(void *argument) {
    verify_tls(1); assert(self()!=main_handle && *error()==0);
    *error()=(int32_t)(uintptr_t)argument;
    unsigned char *static_tls=(unsigned char *)runtime_thread_tcb()-64;
    const uint64_t index[]={2,0};
    static_tls[0]=73; *(unsigned char *)module_tls(index)=91;
    verify_tls(0);
    assert(*error()==(int32_t)(uintptr_t)argument && static_tls[0]==73 && *(unsigned char *)module_tls(index)==91);
    return argument;
}
static void tls_contracts(void) {
    verify_tls(1); *error()=123;
    void *workers[8];
    for (uintptr_t i=0;i<8;++i) assert(create(&workers[i],NULL,tls_worker,(void *)(i+1),"tls-worker")==0);
    for (uintptr_t i=0;i<8;++i) { void *result=NULL; assert(join(workers[i],&result)==0 && result==(void *)(i+1)); }
    verify_tls(1); assert(*error()==123);
    unsigned char code[20]={0};
    const unsigned char pattern[]={0x65,0x48,0x8b,0x04,0x25,0,0,0,0};
    memcpy(code+1,pattern,9); memcpy(code+12,pattern,8);
#ifdef _WIN32
    assert(runtime_thread_patch_tls_loads(code,sizeof(code))==1 && code[0]==0 && code[10]==0 && code[11]==0);
#endif
    assert(!runtime_thread_patch_tls_loads(code,8) && !runtime_thread_patch_tls_loads(NULL,0));
}
static ABI void *echo(void *argument) {
    assert(self()!=main_handle && (uint32_t)join(self(),NULL)==ERR(11)); return argument;
}
static void lifecycle(void) {
    void *t=NULL,*result=NULL;
    assert((uint32_t)create(NULL,NULL,echo,NULL,"bad")==ERR(22));
    assert((uint32_t)create(&t,NULL,NULL,NULL,"bad")==ERR(22));
    assert(create(&t,NULL,echo,(void *)(uintptr_t)37,"echo")==0 && t);
    assert(join(t,&result)==0 && result==(void *)(uintptr_t)37);
    assert((uint32_t)join(t,NULL)==ERR(22) && (uint32_t)detach(t)==ERR(22));
    assert((uint32_t)join(main_handle,NULL)==ERR(3) && (uint32_t)join((void *)(uintptr_t)99,NULL)==ERR(3));
}
static uint32_t destructor_key;
static atomic_int destructor_calls;
static ABI void destructor(void *value) {
    assert(value==(void *)(uintptr_t)73 && !key_get(destructor_key));
    verify_tls(0);
    if (atomic_fetch_add(&destructor_calls,1)<3) assert(key_set(destructor_key,value)==0);
}
static ABI void *key_worker(void *argument) {
    assert(!key_get(destructor_key));
    assert(key_set(destructor_key,argument)==0 && key_get(destructor_key)==argument);
    return argument;
}
static void key_contracts(void) {
    assert(key_create(&destructor_key,destructor)==0);
    assert(key_set(destructor_key,(void *)(uintptr_t)91)==0);
    void *t=NULL; assert(create(&t,NULL,key_worker,(void *)(uintptr_t)73,"keys")==0 && join(t,NULL)==0);
    assert(atomic_load(&destructor_calls)==4 && key_get(destructor_key)==(void *)(uintptr_t)91);
    uint32_t old=destructor_key;
    assert(key_delete(old)==0 && !key_get(old) && (uint32_t)key_set(old,NULL)==ERR(22));
    assert(key_create(&destructor_key,NULL)==0 && destructor_key==old && !key_get(old));
    assert(key_delete(old)==0 && (uint32_t)key_delete(old)==ERR(22));
    assert((uint32_t)key_create(NULL,NULL)==ERR(22) && !key_get(0));
}
static atomic_int once_calls;
static int32_t once_state;
static ABI void once_callback(void) { atomic_fetch_add(&once_calls,1); }
static ABI void *once_worker(void *unused) {
    (void)unused;
    for (int i=0;i<50;++i) assert(once(&once_state,once_callback)==0);
    return NULL;
}
static void once_contracts(void) {
    void *workers[8];
    for (int i=0;i<8;++i) assert(create(&workers[i],NULL,once_worker,NULL,"once")==0);
    for (int i=0;i<8;++i) assert(join(workers[i],NULL)==0);
    assert(once_state==1 && atomic_load(&once_calls)==1);
    int32_t invalid=7;
    assert((uint32_t)once(&invalid,once_callback)==ERR(22) && (uint32_t)once(NULL,once_callback)==ERR(22));
}
static void attributes(void) {
    void *attr=NULL; uint64_t size=0;
    typedef int32_t (ABI *GetSize)(void **,uint64_t *);
    GetSize get_stack=THREAD(GetSize,"-fA+7ZlGDQs#p#J"); assert(get_stack);
    assert(attr_init(&attr)==0 && get_stack(&attr,&size)==0 && size==1024*1024);
    assert((uint32_t)set_stack(&attr,1)==ERR(22) && (uint32_t)set_stack(&attr,UINT64_MAX)==ERR(22));
    assert((uint32_t)set_detach(&attr,2)==ERR(22));
    assert(set_stack(&attr,64*1024)==0 && get_stack(&attr,&size)==0 && size==64*1024);
    void *t=NULL,*result=NULL;
    assert(create(&t,&attr,echo,(void *)(uintptr_t)17,"attributes")==0 && join(t,&result)==0 && result==(void *)(uintptr_t)17);
    assert(attr_destroy(&attr)==0 && !attr && (uint32_t)attr_destroy(&attr)==ERR(22));
}
static atomic_int gate,join_started,join_loser;
static ABI void *gated_worker(void *argument) {
    while (!atomic_load(&gate)) sched_yield();
    return argument;
}
typedef struct { void *target,*value; int32_t status; } Joiner;
static void *join_worker(void *argument) {
    Joiner *j=argument;
    atomic_fetch_add(&join_started,1);
    j->status=join(j->target,&j->value);
    if (j->status) atomic_store(&join_loser,1);
    return NULL;
}
static void join_race(void) {
    void *target=NULL; assert(create(&target,NULL,gated_worker,(void *)(uintptr_t)91,"join-race")==0);
    Joiner records[2]={{target,NULL,0},{target,NULL,0}}; pthread_t callers[2];
    for (int i=0;i<2;++i) assert(pthread_create(&callers[i],NULL,join_worker,&records[i])==0);
    while (atomic_load(&join_started)!=2 || !atomic_load(&join_loser)) sched_yield();
    assert((uint32_t)detach(target)==ERR(22));
    atomic_store(&gate,1);
    for (int i=0;i<2;++i) assert(pthread_join(callers[i],NULL)==0);
    assert((records[0].status==0 && (uint32_t)records[1].status==ERR(22)) ||
           (records[1].status==0 && (uint32_t)records[0].status==ERR(22)));
    assert((records[0].status ? records[1].value : records[0].value)==(void *)(uintptr_t)91);
}
static atomic_int detached_done;
static atomic_int detached_destructor_calls;
static pthread_key_t detached_native_key;
static void detached_native_cleanup(void *value) {
    (void)value;
    assert(!runtime_thread_tcb());
    atomic_store(&detached_done,1);
}
static ABI void detached_destructor(void *value) { (void)value; atomic_fetch_add(&detached_destructor_calls,1); }
static ABI void *detached_worker(void *unused) {
    (void)unused; assert(key_set(destructor_key,(void *)(uintptr_t)1)==0);
    assert(pthread_setspecific(detached_native_key,(void *)(uintptr_t)1)==0);
    while (!atomic_load(&gate)) sched_yield(); return NULL;
}
static void detached_thread(void) {
    assert(pthread_key_create(&detached_native_key,detached_native_cleanup)==0);
    assert(key_create(&destructor_key,detached_destructor)==0);
    void *attr=NULL,*t=NULL; assert(attr_init(&attr)==0 && set_detach(&attr,1)==0);
    assert(create(&t,&attr,detached_worker,NULL,"detached")==0);
    assert((uint32_t)join(t,NULL)==ERR(22) && (uint32_t)detach(t)==ERR(22));
    atomic_store(&gate,1);
    while (!atomic_load(&detached_done)) sched_yield();
    assert(atomic_load(&detached_destructor_calls)==1);
    assert(attr_destroy(&attr)==0);
}
static void raw_exit(void) {
    Exit exit_thread=THREAD(Exit,"3kg7rT0NQIs#p#J"); assert(exit_thread);
    /* sub rsp,8; movabs rax,thread_exit; call rax; ud2. No PE unwind entries. */
    unsigned char code[]={0x48,0x83,0xec,0x08,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x0f,0x0b};
    uintptr_t address=(uintptr_t)exit_thread; memcpy(code+6,&address,8);
    unsigned char *executable=runtime_low_map(sizeof(code),7); assert(executable); memcpy(executable,code,sizeof(code));
#ifdef _WIN32
    assert(FlushInstructionCache(GetCurrentProcess(),executable,sizeof(code)));
#endif
    assert(key_create(&destructor_key,destructor)==0);
    /* No key value is set by the raw code, so it must exit without callbacks. */
    void *t=NULL,*result=NULL;
    assert(create(&t,NULL,(Entry)(uintptr_t)executable,(void *)(uintptr_t)83,"raw-exit")==0);
    assert(join(t,&result)==0 && result==(void *)(uintptr_t)83 && !atomic_load(&destructor_calls));
    assert(key_delete(destructor_key)==0 && runtime_low_unmap(executable,sizeof(code))==0);
}
static void *host_worker(void *argument) {
    runtime_thread_attach_host("external-callback"); verify_tls(1);
    *(void **)argument=runtime_thread_tcb();
    assert(key_set(destructor_key,(void *)(uintptr_t)73)==0);
    return NULL;
}
static void host_cleanup(void) {
    assert(key_create(&destructor_key,destructor)==0);
    void *tcb=NULL; pthread_t thread;
    assert(pthread_create(&thread,NULL,host_worker,&tcb)==0 && pthread_join(thread,NULL)==0);
    assert(tcb && atomic_load(&destructor_calls)==4);
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info;
    assert(VirtualQuery(tcb,&info,sizeof(info)) && info.State==MEM_RESERVE);
#endif
    assert(key_delete(destructor_key)==0);
}
static void *cold_worker(void *argument) {
    runtime_thread_attach_host("cold-host");
    uint64_t *tcb=runtime_thread_tcb();
    assert(tcb && tcb[0]==(uintptr_t)tcb && ((unsigned char *)tcb)[-64]==42);
    *(void **)argument=tcb;
    return NULL;
}
static void cold_start(void) {
    /* First attaches race, before any main attach, patch, key or TCB exists. */
    runtime_set_main_tls(initial,sizeof(initial),64,64);
    pthread_t workers[16]; void *tcbs[16]={0};
    for (int i=0;i<16;++i) assert(pthread_create(&workers[i],NULL,cold_worker,&tcbs[i])==0);
    for (int i=0;i<16;++i) assert(pthread_join(workers[i],NULL)==0 && tcbs[i]);
    for (int i=0;i<16;++i) for (int j=i+1;j<16;++j) assert(tcbs[i]!=tcbs[j]);
    assert(!runtime_thread_tcb());
}
int main(int argc,char **argv) {
    assert(argc==2);
    if (!strcmp(argv[1],"cold-start")) { cold_start(); puts("PASS: cold thread attach"); return 0; }
#ifdef _WIN32
    if (!strcmp(argv[1],"reserved-slot")) {
        runtime_thread_prepare_host();
        DWORD slot;
        do { slot=TlsAlloc(); assert(slot!=TLS_OUT_OF_INDEXES); } while (slot<64);
        setup();
        tls_contracts();
        puts("PASS: reserved guest TLS survives exhausted direct slots");
        return 0;
    }
    if (!strcmp(argv[1],"no-slot")) {
        DWORD slot;
        do { slot=TlsAlloc(); assert(slot!=TLS_OUT_OF_INDEXES); } while (slot<64);
        unsigned char code[]={0x65,0x48,0x8b,0x04,0x25,0,0,0,0};
        runtime_thread_patch_tls_loads(code,sizeof(code));
        assert(!"exhausted direct TLS slots should be rejected");
    }
#endif
    setup();
    if (!strcmp(argv[1],"tls")) tls_contracts();
    else if (!strcmp(argv[1],"lifecycle")) lifecycle();
    else if (!strcmp(argv[1],"keys")) key_contracts();
    else if (!strcmp(argv[1],"once")) once_contracts();
    else if (!strcmp(argv[1],"attributes")) attributes();
    else if (!strcmp(argv[1],"join-race")) join_race();
    else if (!strcmp(argv[1],"detached")) detached_thread();
    else if (!strcmp(argv[1],"raw-exit")) raw_exit();
    else if (!strcmp(argv[1],"host-cleanup")) host_cleanup();
    else if (!strcmp(argv[1],"bad-main")) runtime_set_main_tls(initial,3,2,16);
    else if (!strcmp(argv[1],"bad-module")) runtime_set_module_tls(2,initial,3,2);
    else if (!strcmp(argv[1],"bad-index")) { const uint64_t index[]={2,32}; module_tls(index); }
    else if (!strcmp(argv[1],"bad-reconfigure")) {
        const uint64_t index[]={2,0}; module_tls(index); runtime_set_libc_tls(initial,3,32);
    }
    else assert(!"unknown mode");
    puts("PASS: guest threads/TLS"); return 0;
}
