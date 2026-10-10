/* Optional guest wait diagnostics shared by the memory and synchronization runtime. */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_clock.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

static pthread_once_t enabled_once = PTHREAD_ONCE_INIT;
static int enabled;
static void configure(void) {
    const char *flag = getenv("BB_FRAME_STATS");
    enabled = flag && flag[0] && strcmp(flag, "0") && strcmp(flag, "off");
}
uint64_t runtime_wait_clock(void) {
    pthread_once(&enabled_once, configure);
    return enabled ? runtime_monotonic_ns() : 0;
}

void runtime_guest_call_sites(uint64_t out[3]) {
    const uintptr_t text_lo = UINT64_C(0x800000000), text_hi = text_lo + UINT64_C(0x50d96dc);
    out[0] = out[1] = out[2] = 0;
    const uintptr_t *sp = (const uintptr_t *)__builtin_frame_address(0);
    uintptr_t end = 0;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(sp, &info, sizeof(info)) && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        end = (uintptr_t)info.BaseAddress + info.RegionSize;
#else
    pthread_attr_t attr;
    if (!pthread_getattr_np(pthread_self(), &attr)) {
        void *base = NULL; size_t size = 0;
        if (!pthread_attr_getstack(&attr, &base, &size)) end = (uintptr_t)base + size;
        pthread_attr_destroy(&attr);
    }
#endif
    for (unsigned i = 0, found = 0; i < 400 && found < 3 && (uintptr_t)(sp+i+1) <= end; ++i)
        if (sp[i] >= text_lo && sp[i] < text_hi) out[found++] = sp[i] - text_lo;
}

typedef struct { uint64_t sites[3], tid, count, ns; int kind; } WaitSite;
static WaitSite sites[512];
static pthread_mutex_t sites_lock = PTHREAD_MUTEX_INITIALIZER;
void runtime_wait_note(int kind, uint64_t ns) {
    pthread_once(&enabled_once, configure);
    if (!enabled || !ns || kind < 0 || kind > 3) return;
    WaitSite sample = {.kind = kind, .count = 1, .ns = ns};
#ifdef _WIN32
    sample.tid = GetCurrentThreadId();
#else
    sample.tid = (uint64_t)syscall(SYS_gettid);
#endif
    runtime_guest_call_sites(sample.sites);
    pthread_mutex_lock(&sites_lock);
    for (unsigned i = 0; i < 512; ++i) {
        WaitSite *row = &sites[i];
        if (!row->count) { *row = sample; break; }
        if (row->tid == sample.tid && row->kind == kind && !memcmp(row->sites,sample.sites,sizeof(row->sites))) {
            ++row->count; row->ns += ns; break;
        }
    }
    pthread_mutex_unlock(&sites_lock);
}
void runtime_wait_report(double frames) {
    pthread_once(&enabled_once, configure);
    if (!enabled || frames <= 0) return;
    WaitSite copy[512];
    pthread_mutex_lock(&sites_lock);
    memcpy(copy, sites, sizeof(copy)); memset(sites,0,sizeof(sites));
    pthread_mutex_unlock(&sites_lock);
    static const char *kinds[] = {"cond", "mutex", "sema", "sleep"};
    for (unsigned i = 0; i < 512; ++i) if (copy[i].count)
        printf("Guest wait: tid=%" PRIu64 " %s +%#" PRIx64 "<+%#" PRIx64 "<+%#" PRIx64 " %.1fx %.3f ms/frame\n",
               copy[i].tid,kinds[copy[i].kind],copy[i].sites[0],copy[i].sites[1],copy[i].sites[2],
               copy[i].count/frames,copy[i].ns/(frames*1e6));
}
