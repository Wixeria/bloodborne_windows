#ifndef BB_RUNTIME_CLOCK_H
#define BB_RUNTIME_CLOCK_H
#include <stdint.h>

/* Host-only helpers, never exposed directly through a guest import.
 * sleep_until returns a host errno code and handles interrupted Linux sleeps. */
uint64_t runtime_monotonic_ns(void);
int runtime_sleep_until(uint64_t monotonic_deadline_ns);
#endif
