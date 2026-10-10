#ifndef BB_RUNTIME_THREAD_H
#define BB_RUNTIME_THREAD_H
#include "runtime.h"

/* Reserve host resources before loading graphics drivers, which can consume
   all direct Windows TLS slots. Does not attach a guest thread or freeze TLS. */
void runtime_thread_prepare_host(void);

/* Loader: call on executable spans while writable, before any guest execution.
   On Windows, rewrites prepared mov rax,gs:[0] into this process's allocated
   TEB TLS slot. Never apply these patched bytes to another process. */
size_t runtime_thread_patch_tls_loads(void *code,size_t size);
void *runtime_thread_tcb(void);
#endif
