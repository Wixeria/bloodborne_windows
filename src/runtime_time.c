/* Explicit guest timeval layout; host pointers never escape this boundary. */
#define _DEFAULT_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/time.h>
#endif
typedef struct { int64_t seconds, microseconds; } GuestTimeval;
typedef struct { int32_t minuteswest, dsttime; } GuestTimezone;
static ABI int guest_gettimeofday(GuestTimeval *value,GuestTimezone *zone) {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t ticks=((uint64_t)ft.dwHighDateTime<<32)|ft.dwLowDateTime;
    /* FILETIME: 100 ns since 1601; the guest uses Unix seconds and useconds. */
    uint64_t usec=(ticks-UINT64_C(116444736000000000))/10;
    if (value) { value->seconds=(int64_t)(usec/1000000); value->microseconds=(int64_t)(usec%1000000); }
    if (zone) {
        TIME_ZONE_INFORMATION z;
        DWORD state=GetTimeZoneInformation(&z);
        if (state==TIME_ZONE_ID_INVALID) { errno=EIO; return -1; }
        zone->minuteswest=z.Bias+(state==TIME_ZONE_ID_DAYLIGHT ? z.DaylightBias : z.StandardBias);
        zone->dsttime=state==TIME_ZONE_ID_DAYLIGHT;
    }
#else
    struct timeval t;
    struct timezone z;
    if (gettimeofday(&t,&z)) {
        /* Guest errno/TLS failure path is not implemented yet. */
        perror("STOP: host gettimeofday failed"); exit(21);
    }
    if (value) { value->seconds=t.tv_sec; value->microseconds=t.tv_usec; }
    if (zone) { zone->minuteswest=z.tz_minuteswest; zone->dsttime=z.tz_dsttime; }
#endif
    return 0;
}
uintptr_t runtime_time_resolve(const char *name) {
    if (!strcmp(name,"n88vx3C5nW8#I#J")) return (uintptr_t)guest_gettimeofday;
    return 0;
}
