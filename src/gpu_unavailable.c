/* CPU loader build: do not resolve graphics imports without the real renderer. */
#include "gpu/bbgpu.h"
#include <stdio.h>
void bbgpu_register_kernel(void) {}
void bbgpu_patch_image(unsigned char *image, uint64_t size) { (void)image; (void)size; }
int bbgpu_init(const BbGpuConfig *config) {
    (void)config;
    fputs("Windows game renderer is not integrated; use --cpu-only for loader diagnostics.\n",stderr);
    return 1;
}
uintptr_t bbgpu_resolve(const char *name) { (void)name; return 0; }
int bbgpu_handle_fault(void *context,void *address) { (void)context; (void)address; return 0; }
void bbgpu_dump_guest_writes(void *context) { (void)context; }
int bbgpu_text_input_begin(const char *initial,const char *prompt) { (void)initial; (void)prompt; return 0; }
int bbgpu_text_input_poll(char *out,uint64_t size) { (void)out; (void)size; return 2; }
int bbgpu_overlay_captures_input(void) { return 0; }
void bbgpu_reflex_input_sample(uintptr_t caller) { (void)caller; }
void bbgpu_reflex_window_message(uint32_t message) { (void)message; }
unsigned bbgpu_symbol_count(void) { return 0; }
