/* Guest once/key contracts, independent of Linux process services. */
#include "runtime.h"
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
static ABI int32_t thread_once(int32_t *once,void (ABI *routine)(void)) {
    if (!once || !routine) return ERR(22);
    for (;;) {
        int32_t state=__atomic_load_n(once,__ATOMIC_ACQUIRE);
        if (state==1) return 0;
        if (state!=0 && state!=2) return ERR(22);
        if (state==0 && __atomic_compare_exchange_n(once,&state,2,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
            routine(); __atomic_store_n(once,1,__ATOMIC_RELEASE); return 0;
        }
        sched_yield();
    }
}
static ABI int32_t posix_once(int32_t *once,void (ABI *routine)(void)) {
    return thread_once(once,routine) ? 22 : 0;
}
#define KEYS 256
typedef void (ABI *KeyDestructor)(void *);
static struct { int used; uint64_t generation; KeyDestructor destructor; } keys[KEYS];
static pthread_mutex_t key_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local struct { void *value; uint64_t generation; } values[KEYS];
static ABI int32_t key_create(uint32_t *key,KeyDestructor destructor) {
    if (!key) return ERR(22);
    pthread_mutex_lock(&key_lock);
    for (uint32_t i=1;i<KEYS;++i) if (!keys[i].used) {
        keys[i].used=1; ++keys[i].generation; keys[i].destructor=destructor;
        pthread_mutex_unlock(&key_lock); *key=i; return 0;
    }
    pthread_mutex_unlock(&key_lock); return ERR(35);
}
static ABI int32_t key_delete(uint32_t key) {
    if (!key || key>=KEYS) return ERR(22);
    pthread_mutex_lock(&key_lock);
    int32_t result=keys[key].used ? 0 : ERR(22);
    keys[key].used=0; keys[key].destructor=NULL;
    pthread_mutex_unlock(&key_lock); return result;
}
static ABI int32_t key_set(uint32_t key,void *value) {
    if (!key || key>=KEYS) return ERR(22);
    pthread_mutex_lock(&key_lock);
    int32_t result=keys[key].used ? 0 : ERR(22);
    if (!result) { values[key].value=value; values[key].generation=keys[key].generation; }
    pthread_mutex_unlock(&key_lock); return result;
}
static ABI void *key_get(uint32_t key) {
    if (!key || key>=KEYS) return NULL;
    pthread_mutex_lock(&key_lock);
    void *result=keys[key].used && values[key].generation==keys[key].generation ? values[key].value : NULL;
    pthread_mutex_unlock(&key_lock); return result;
}
void runtime_thread_keys_cleanup(void) {
    for (int round=0;round<4;++round) {
        int any=0;
        for (uint32_t i=1;i<KEYS;++i) {
            pthread_mutex_lock(&key_lock);
            void *value=values[i].value;
            KeyDestructor destructor=keys[i].used && values[i].generation==keys[i].generation ? keys[i].destructor : NULL;
            values[i].value=NULL;
            pthread_mutex_unlock(&key_lock);
            if (value && destructor) { any=1; destructor(value); }
        }
        if (!any) break;
    }
    for (uint32_t i=1;i<KEYS;++i) values[i].value=NULL;
}
static ABI int32_t posix_key_create(uint32_t *key,KeyDestructor destructor) {
    int32_t r=key_create(key,destructor); return r ? r&0xffff : 0;
}
static ABI int32_t posix_key_delete(uint32_t key) { int32_t r=key_delete(key); return r ? r&0xffff : 0; }
static ABI int32_t posix_key_set(uint32_t key,void *value) { int32_t r=key_set(key,value); return r ? r&0xffff : 0; }
uintptr_t runtime_thread_keys_resolve(const char *name) {
    static const RuntimeExport exports[]={
        {"scePthreadOnce",thread_once},{"pthread_once",posix_once},
        {"scePthreadKeyCreate",key_create},{"scePthreadKeyDelete",key_delete},
        {"scePthreadSetspecific",key_set},{"scePthreadGetspecific",key_get},
        {"pthread_key_create",posix_key_create},{"pthread_key_delete",posix_key_delete},
        {"pthread_setspecific",posix_key_set},{"pthread_getspecific",key_get},
    };
    return RUNTIME_LOOKUP(exports,name);
}
