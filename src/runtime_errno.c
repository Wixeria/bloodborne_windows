#include "runtime.h"
/* Memory imports also report POSIX errors before any guest thread is attached. */
static _Thread_local int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
