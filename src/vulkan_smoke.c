/* Independent backend smoke test: command submission and readback, no game graphics. */
#ifdef _WIN32
#define VK_NO_PROTOTYPES
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "Vulkan: %s returned %d\n", #call, r); exit(1); } } while (0)

#ifdef _WIN32
static PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
static PFN_vkAllocateMemory vkAllocateMemory;
static PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
static PFN_vkBindBufferMemory vkBindBufferMemory;
static PFN_vkCmdFillBuffer vkCmdFillBuffer;
static PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
static PFN_vkCreateBuffer vkCreateBuffer;
static PFN_vkCreateCommandPool vkCreateCommandPool;
static PFN_vkCreateDevice vkCreateDevice;
static PFN_vkCreateInstance vkCreateInstance;
static PFN_vkDestroyBuffer vkDestroyBuffer;
static PFN_vkDestroyCommandPool vkDestroyCommandPool;
static PFN_vkDestroyDevice vkDestroyDevice;
static PFN_vkDestroyInstance vkDestroyInstance;
static PFN_vkEndCommandBuffer vkEndCommandBuffer;
static PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
static PFN_vkFreeMemory vkFreeMemory;
static PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
static PFN_vkGetDeviceQueue vkGetDeviceQueue;
static PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
static PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkMapMemory vkMapMemory;
static PFN_vkQueueSubmit vkQueueSubmit;
static PFN_vkQueueWaitIdle vkQueueWaitIdle;
static PFN_vkUnmapMemory vkUnmapMemory;
static int load_vulkan(void) {
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader) { fputs("Vulkan: system vulkan-1.dll is unavailable\n",stderr); return 0; }
    vkAllocateCommandBuffers=(PFN_vkAllocateCommandBuffers)(void *)GetProcAddress(loader,"vkAllocateCommandBuffers");
    if (!vkAllocateCommandBuffers) { fputs("Vulkan: missing vkAllocateCommandBuffers\n",stderr); return 0; }
    vkAllocateMemory=(PFN_vkAllocateMemory)(void *)GetProcAddress(loader,"vkAllocateMemory");
    if (!vkAllocateMemory) { fputs("Vulkan: missing vkAllocateMemory\n",stderr); return 0; }
    vkBeginCommandBuffer=(PFN_vkBeginCommandBuffer)(void *)GetProcAddress(loader,"vkBeginCommandBuffer");
    if (!vkBeginCommandBuffer) { fputs("Vulkan: missing vkBeginCommandBuffer\n",stderr); return 0; }
    vkBindBufferMemory=(PFN_vkBindBufferMemory)(void *)GetProcAddress(loader,"vkBindBufferMemory");
    if (!vkBindBufferMemory) { fputs("Vulkan: missing vkBindBufferMemory\n",stderr); return 0; }
    vkCmdFillBuffer=(PFN_vkCmdFillBuffer)(void *)GetProcAddress(loader,"vkCmdFillBuffer");
    if (!vkCmdFillBuffer) { fputs("Vulkan: missing vkCmdFillBuffer\n",stderr); return 0; }
    vkCmdPipelineBarrier=(PFN_vkCmdPipelineBarrier)(void *)GetProcAddress(loader,"vkCmdPipelineBarrier");
    if (!vkCmdPipelineBarrier) { fputs("Vulkan: missing vkCmdPipelineBarrier\n",stderr); return 0; }
    vkCreateBuffer=(PFN_vkCreateBuffer)(void *)GetProcAddress(loader,"vkCreateBuffer");
    if (!vkCreateBuffer) { fputs("Vulkan: missing vkCreateBuffer\n",stderr); return 0; }
    vkCreateCommandPool=(PFN_vkCreateCommandPool)(void *)GetProcAddress(loader,"vkCreateCommandPool");
    if (!vkCreateCommandPool) { fputs("Vulkan: missing vkCreateCommandPool\n",stderr); return 0; }
    vkCreateDevice=(PFN_vkCreateDevice)(void *)GetProcAddress(loader,"vkCreateDevice");
    if (!vkCreateDevice) { fputs("Vulkan: missing vkCreateDevice\n",stderr); return 0; }
    vkCreateInstance=(PFN_vkCreateInstance)(void *)GetProcAddress(loader,"vkCreateInstance");
    if (!vkCreateInstance) { fputs("Vulkan: missing vkCreateInstance\n",stderr); return 0; }
    vkDestroyBuffer=(PFN_vkDestroyBuffer)(void *)GetProcAddress(loader,"vkDestroyBuffer");
    if (!vkDestroyBuffer) { fputs("Vulkan: missing vkDestroyBuffer\n",stderr); return 0; }
    vkDestroyCommandPool=(PFN_vkDestroyCommandPool)(void *)GetProcAddress(loader,"vkDestroyCommandPool");
    if (!vkDestroyCommandPool) { fputs("Vulkan: missing vkDestroyCommandPool\n",stderr); return 0; }
    vkDestroyDevice=(PFN_vkDestroyDevice)(void *)GetProcAddress(loader,"vkDestroyDevice");
    if (!vkDestroyDevice) { fputs("Vulkan: missing vkDestroyDevice\n",stderr); return 0; }
    vkDestroyInstance=(PFN_vkDestroyInstance)(void *)GetProcAddress(loader,"vkDestroyInstance");
    if (!vkDestroyInstance) { fputs("Vulkan: missing vkDestroyInstance\n",stderr); return 0; }
    vkEndCommandBuffer=(PFN_vkEndCommandBuffer)(void *)GetProcAddress(loader,"vkEndCommandBuffer");
    if (!vkEndCommandBuffer) { fputs("Vulkan: missing vkEndCommandBuffer\n",stderr); return 0; }
    vkEnumeratePhysicalDevices=(PFN_vkEnumeratePhysicalDevices)(void *)GetProcAddress(loader,"vkEnumeratePhysicalDevices");
    if (!vkEnumeratePhysicalDevices) { fputs("Vulkan: missing vkEnumeratePhysicalDevices\n",stderr); return 0; }
    vkFreeMemory=(PFN_vkFreeMemory)(void *)GetProcAddress(loader,"vkFreeMemory");
    if (!vkFreeMemory) { fputs("Vulkan: missing vkFreeMemory\n",stderr); return 0; }
    vkGetBufferMemoryRequirements=(PFN_vkGetBufferMemoryRequirements)(void *)GetProcAddress(loader,"vkGetBufferMemoryRequirements");
    if (!vkGetBufferMemoryRequirements) { fputs("Vulkan: missing vkGetBufferMemoryRequirements\n",stderr); return 0; }
    vkGetDeviceQueue=(PFN_vkGetDeviceQueue)(void *)GetProcAddress(loader,"vkGetDeviceQueue");
    if (!vkGetDeviceQueue) { fputs("Vulkan: missing vkGetDeviceQueue\n",stderr); return 0; }
    vkGetPhysicalDeviceMemoryProperties=(PFN_vkGetPhysicalDeviceMemoryProperties)(void *)GetProcAddress(loader,"vkGetPhysicalDeviceMemoryProperties");
    if (!vkGetPhysicalDeviceMemoryProperties) { fputs("Vulkan: missing vkGetPhysicalDeviceMemoryProperties\n",stderr); return 0; }
    vkGetPhysicalDeviceProperties=(PFN_vkGetPhysicalDeviceProperties)(void *)GetProcAddress(loader,"vkGetPhysicalDeviceProperties");
    if (!vkGetPhysicalDeviceProperties) { fputs("Vulkan: missing vkGetPhysicalDeviceProperties\n",stderr); return 0; }
    vkGetPhysicalDeviceQueueFamilyProperties=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)(void *)GetProcAddress(loader,"vkGetPhysicalDeviceQueueFamilyProperties");
    if (!vkGetPhysicalDeviceQueueFamilyProperties) { fputs("Vulkan: missing vkGetPhysicalDeviceQueueFamilyProperties\n",stderr); return 0; }
    vkMapMemory=(PFN_vkMapMemory)(void *)GetProcAddress(loader,"vkMapMemory");
    if (!vkMapMemory) { fputs("Vulkan: missing vkMapMemory\n",stderr); return 0; }
    vkQueueSubmit=(PFN_vkQueueSubmit)(void *)GetProcAddress(loader,"vkQueueSubmit");
    if (!vkQueueSubmit) { fputs("Vulkan: missing vkQueueSubmit\n",stderr); return 0; }
    vkQueueWaitIdle=(PFN_vkQueueWaitIdle)(void *)GetProcAddress(loader,"vkQueueWaitIdle");
    if (!vkQueueWaitIdle) { fputs("Vulkan: missing vkQueueWaitIdle\n",stderr); return 0; }
    vkUnmapMemory=(PFN_vkUnmapMemory)(void *)GetProcAddress(loader,"vkUnmapMemory");
    if (!vkUnmapMemory) { fputs("Vulkan: missing vkUnmapMemory\n",stderr); return 0; }
    /* The loader stays resident until process exit; device/instance are destroyed below. */
    return 1;
}
#endif
int vulkan_smoke(void) {
#ifdef _WIN32
    if (!load_vulkan()) return 1;
#endif
    VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="Bloodborne native probe", .apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo ici = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
    VkInstance instance; CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 0; CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) { fprintf(stderr, "Vulkan: no physical devices\n"); vkDestroyInstance(instance, NULL); return 1; }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) exit(1);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice physical = devices[0]; free(devices);
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
    uint32_t nq = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, NULL);
    VkQueueFamilyProperties *families = calloc(nq, sizeof(*families));
    if (!families) exit(1);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &nq, families);
    uint32_t family = 0;
    while (family < nq && !(families[family].queueCount && (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))) ++family;
    free(families);
    if (family == nq) { fprintf(stderr, "Vulkan: no graphics queue\n"); vkDestroyInstance(instance, NULL); return 1; }
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority};
    VkDeviceCreateInfo dci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci};
    VkDevice device; CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    VkBufferCreateInfo bci = {.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=4096, .usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer buffer; CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t type = 0, wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < memory.memoryTypeCount && !((requirements.memoryTypeBits & (1u << type)) && (memory.memoryTypes[type].propertyFlags & wanted) == wanted)) ++type;
    if (type == memory.memoryTypeCount) { fprintf(stderr, "Vulkan: no coherent host memory\n"); exit(1); }
    VkMemoryAllocateInfo mai = {.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=requirements.size, .memoryTypeIndex=type};
    VkDeviceMemory allocation; CHECK(vkAllocateMemory(device, &mai, NULL, &allocation));
    CHECK(vkBindBufferMemory(device, buffer, allocation, 0));
    VkCommandPoolCreateInfo pci = {.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex=family};
    VkCommandPool pool; CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool=pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1};
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device, &cai, &command));
    VkCommandBufferBeginInfo begin = {.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    vkCmdFillBuffer(command, buffer, 0, 4096, 0xb100db0e);
    VkMemoryBarrier barrier = {.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit = {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount=1, .pCommandBuffers=&command};
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    uint32_t *pixels; CHECK(vkMapMemory(device, allocation, 0, 4096, 0, (void **)&pixels));
    int ok = 1; for (int i=0; i<1024; ++i) if (pixels[i] != 0xb100db0e) ok = 0;
    vkUnmapMemory(device, allocation);
    printf("Vulkan: %s; command submission + 4096-byte readback %s\n", properties.deviceName, ok ? "PASS" : "FAIL");
    vkDestroyCommandPool(device, pool, NULL); vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, allocation, NULL); vkDestroyDevice(device, NULL); vkDestroyInstance(instance, NULL);
    return ok ? 0 : 1;
}
