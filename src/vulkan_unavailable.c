#include <stdio.h>
int vulkan_smoke(void) {
    fputs("Vulkan smoke test was not built; configure BB_VULKAN_HEADERS or install the Vulkan SDK.\n",stderr);
    return 1;
}
