/* Linked PS4 modules use __tls_get_addr; each host thread gets its own block. */
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TLS_MODULES 8
static struct { const void *data; uint64_t filesz,memsz; int configured,used; } templates[TLS_MODULES];
static pthread_mutex_t tls_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local struct { unsigned char *data; size_t size; } blocks[TLS_MODULES];
static void tls_stop(const char *reason) { fprintf(stderr,"STOP: %s\n",reason); exit(21); }
void runtime_set_module_tls(uint64_t module,const void *data,uint64_t filesz,uint64_t memsz) {
    if (module<2 || module>=TLS_MODULES || !memsz || memsz>1024*1024 || filesz>memsz || (filesz && !data))
        tls_stop("unsupported TLS module layout");
    pthread_mutex_lock(&tls_lock);
    if (templates[module].used) { pthread_mutex_unlock(&tls_lock); tls_stop("TLS module reconfigured after first use"); }
    templates[module].data=data; templates[module].filesz=filesz; templates[module].memsz=memsz;
    templates[module].configured=1;
    pthread_mutex_unlock(&tls_lock);
}
void runtime_set_libc_tls(const void *data,uint64_t filesz,uint64_t memsz) {
    runtime_set_module_tls(2,data,filesz,memsz);
}
static ABI void *guest_tls_get_addr(const uint64_t *index) {
    if (!index || index[0]<2 || index[0]>=TLS_MODULES) tls_stop("unsupported TLS module/offset");
    uint64_t module=index[0],offset=index[1];
    pthread_mutex_lock(&tls_lock);
    if (!templates[module].configured || offset>=templates[module].memsz) {
        pthread_mutex_unlock(&tls_lock); tls_stop("unsupported TLS module/offset");
    }
    const void *initial=templates[module].data;
    size_t filesz=(size_t)templates[module].filesz,memsz=(size_t)templates[module].memsz;
    templates[module].used=1;
    pthread_mutex_unlock(&tls_lock);
    if (!blocks[module].data) {
#ifdef _WIN32
        blocks[module].data=runtime_low_map(memsz,3);
#else
        blocks[module].data=calloc(1,memsz);
#endif
        if (!blocks[module].data) tls_stop("cannot allocate module TLS");
        blocks[module].size=memsz;
        if (filesz) memcpy(blocks[module].data,initial,filesz);
    }
    return blocks[module].data+offset;
}
void runtime_tls_cleanup(void) {
    for (size_t i=2;i<TLS_MODULES;++i) if (blocks[i].data) {
#ifdef _WIN32
        if (runtime_low_unmap(blocks[i].data,blocks[i].size)) tls_stop("cannot release module TLS");
#else
        free(blocks[i].data);
#endif
        blocks[i].data=NULL; blocks[i].size=0;
    }
}
uintptr_t runtime_tls_resolve(const char *name) {
    return !strcmp(name,"vNe1w4diLCs#p#J") ? (uintptr_t)guest_tls_get_addr : 0;
}
