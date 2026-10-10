#ifndef BB_RUNTIME_FAULT_H
#define BB_RUNTIME_FAULT_H
#include <setjmp.h>
#ifdef _WIN32
typedef jmp_buf BbFaultBuffer;
/* Do not unwind arbitrary guest frames through Windows SEH metadata. */
#define bb_fault_setjmp(buffer) _setjmp((buffer), NULL)
#else
typedef sigjmp_buf BbFaultBuffer;
#define bb_fault_setjmp(buffer) sigsetjmp((buffer), 0)
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern __thread BbFaultBuffer *runtime_fault_recover;
#ifdef __cplusplus
}
#endif
/* Called only for memory faults after the renderer had a chance to resolve
   page tracking. Clear the pointer before jumping so a later fault is fatal. */
#ifdef _WIN32
static inline int bb_fault_try_recover(void) {
    BbFaultBuffer *recover=runtime_fault_recover;
    if (!recover) return 0;
    runtime_fault_recover=0;
    longjmp(*recover,1);
}
#endif
#endif
