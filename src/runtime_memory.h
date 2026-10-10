#ifndef BB_RUNTIME_MEMORY_INTERFACE_H
#define BB_RUNTIME_MEMORY_INTERFACE_H
#include "runtime.h"

/* Host helper protection bits, matching Linux PROT_* on the supported x86-64 ABI. */
enum { BB_MEMORY_READ=1, BB_MEMORY_WRITE=2, BB_MEMORY_EXECUTE=4 };
typedef void (*RuntimeGpuRange)(uintptr_t address, uint64_t size);
void runtime_memory_set_gpu_hooks(RuntimeGpuRange map, RuntimeGpuRange unmap, RuntimeGpuRange invalidate);
int runtime_memory_write_backing(uintptr_t address, const void *data, uint64_t size);
void runtime_memory_read_backing(uintptr_t address, void *data, uint64_t size);
/* Unprotected direct-memory view for GPU host import. Allocate/commit the entire
 * imported range first; keep it alive until Vulkan releases it. No allocation. */
void *runtime_memory_backing_pointer(uint64_t phys);
void runtime_memory_set_guest_chunk_allocator(int (*allocate)(uint64_t phys,uint64_t size));
void runtime_memory_trap(uintptr_t address,uint64_t size,unsigned reason,int on);
unsigned runtime_memory_trap_reasons(uintptr_t address);
uint64_t runtime_memory_clamp(uintptr_t address, uint64_t size);
int runtime_memory_region(uintptr_t address, uintptr_t *start, uintptr_t *end, int *mapped);
void runtime_memory_gpu_protect(uintptr_t address, uint64_t size, int read, int write);
extern uint64_t runtime_disabled_optimizations;
#endif
