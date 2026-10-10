/* SPDX-License-Identifier: GPL-2.0-or-later
 * Verify the NVIDIA memory model's prerequisite using two Windows section views.
 * No game files, window, sparse resources or production settings are used.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "runtime_memory.h"

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #call, r); exit(1); } } while (0)
#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "Failed: %s\n", #condition); exit(1); } } while (0)

int main(void) {
    VkInstance instance;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
    CHECK(vkCreateInstance(&ici, NULL, &instance));
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    VkPhysicalDevice* devices = calloc(count, sizeof(*devices));
    REQUIRE(devices != NULL && count != 0);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (props.vendorID == 0x10de) { physical = devices[i]; break; }
    }
    if (!physical) physical = devices[0];
    free(devices);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    printf("GPU: %s\n", props.deviceName);
    CHECK(vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL));
    VkExtensionProperties* extensions = calloc(count, sizeof(*extensions));
    REQUIRE(extensions != NULL);
    CHECK(vkEnumerateDeviceExtensionProperties(physical, NULL, &count, extensions));
    int supported = 0;
    for (uint32_t i = 0; i < count; ++i)
        supported |= strcmp(extensions[i].extensionName, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME) == 0;
    free(extensions);
    if (!supported) { puts("SKIP: VK_EXT_external_memory_host unavailable");
        vkDestroyInstance(instance, NULL); return 77; }
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT host = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
    VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &host};
    vkGetPhysicalDeviceProperties2(physical, &p2);
    printf("Imported pointer alignment: %llu\n", (unsigned long long)host.minImportedHostPointerAlignment);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties* queues = calloc(count, sizeof(*queues));
    REQUIRE(queues != NULL);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues);
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i)
        if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_TRANSFER_BIT)) { family = i; break; }
    free(queues);
    REQUIRE(family != UINT32_MAX);
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    const char* name = VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME;
    VkPhysicalDeviceBufferDeviceAddressFeatures address_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &address_features};
    vkGetPhysicalDeviceFeatures2(physical, &features);
    REQUIRE(address_features.bufferDeviceAddress);
    address_features.bufferDeviceAddressCaptureReplay = VK_FALSE;
    address_features.bufferDeviceAddressMultiDevice = VK_FALSE;
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &address_features,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = &name};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    PFN_vkGetMemoryHostPointerPropertiesEXT get_props =
        (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(device, "vkGetMemoryHostPointerPropertiesEXT");
    REQUIRE(get_props != NULL);
    VkExternalMemoryBufferCreateInfo external = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT};
    const VkDeviceSize size = 32ull << 20;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &external,
        .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkDeviceSize alignment = host.minImportedHostPointerAlignment;
    REQUIRE(alignment != 0);
    VkDeviceSize allocation_size = (req.size + alignment - 1) / alignment * alignment;
    typedef uint64_t (ABI *DirectSize)(void);
    typedef int32_t (ABI *Allocate)(int64_t, int64_t, uint64_t, uint64_t, int, int64_t*);
    typedef int32_t (ABI *Map)(void**, uint64_t, int, int, int64_t, uint64_t);
    typedef int32_t (ABI *Release)(uint64_t, uint64_t);
    DirectSize direct_size = (DirectSize)runtime_memory_resolve("pO96TwzOm5E#p#J");
    Allocate allocate = (Allocate)runtime_memory_resolve("rTXw65xmLIA#p#J");
    Map map = (Map)runtime_memory_resolve("L-Q3LEjIbgA#p#J");
    Release release = (Release)runtime_memory_resolve("MBuItvba6z8#p#J");
    REQUIRE(direct_size && allocate && map && release);
    if (alignment < 16384) alignment = 16384;
    allocation_size = (allocation_size + alignment - 1) / alignment * alignment;
    int64_t phys = -1;
    REQUIRE(allocate(0, (int64_t)direct_size(), allocation_size, alignment, 0, &phys) == 0);
    void* view = NULL;
    REQUIRE(map(&view, allocation_size, 3, 0, phys, alignment) == 0);
    uint32_t* guest = view;
    uint32_t* backing = runtime_memory_backing_pointer((uint64_t)phys);
    REQUIRE(backing != NULL && guest != NULL && (uintptr_t)backing % alignment == 0);
    REQUIRE((uintptr_t)guest < (UINT64_C(1) << 40));
    printf("Runtime pool: phys %#llx; 40-bit guest view and unprotected backing imported\n",
           (unsigned long long)phys);
    VkMemoryHostPointerPropertiesEXT pointer = {.sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT};
    CHECK(get_props(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, backing, &pointer));
    VkPhysicalDeviceMemoryProperties memory_props;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory_props);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & pointer.memoryTypeBits & (1u << i)) &&
            (memory_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
    REQUIRE(type != UINT32_MAX);
    VkMemoryAllocateFlagsInfo flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkImportMemoryHostPointerInfoEXT import = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .pNext = &flags,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, .pHostPointer = backing};
    VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &import,
        .allocationSize = allocation_size, .memoryTypeIndex = type};
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(device, &mai, NULL, &memory));
    CHECK(vkBindBufferMemory(device, buffer, memory, 0));
    PFN_vkGetBufferDeviceAddress get_address =
        (PFN_vkGetBufferDeviceAddress)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress");
    REQUIRE(get_address != NULL);
    VkBufferDeviceAddressInfo address_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer};
    VkDeviceAddress device_address = get_address(device, &address_info);
    REQUIRE(device_address != 0);
    printf("Imported buffer device address: %#llx\n", (unsigned long long)device_address);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &cai, &cmd));
    for (unsigned pass = 0; pass < 3; ++pass) {
        uint32_t cpu_value = 0x12340000u + pass;
        for (size_t i = 0; i < size / 4; ++i) guest[i] = cpu_value;
        REQUIRE(backing[size / 8] == cpu_value);
        // The runtime maps one 16 KiB section view per guest page. A single
        // VirtualProtect across independent views fails; use its range helper.
        runtime_memory_gpu_protect((uintptr_t)guest, size, 1, 0);
        MEMORY_BASIC_INFORMATION first_page, last_page;
        REQUIRE(VirtualQuery(guest, &first_page, sizeof(first_page)) != 0);
        REQUIRE(VirtualQuery((char*)guest + size - 1, &last_page, sizeof(last_page)) != 0);
        REQUIRE(first_page.Protect == PAGE_READONLY && last_page.Protect == PAGE_READONLY);
        CHECK(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(vkBeginCommandBuffer(cmd, &begin));
        // Read CPU-written first half through the GPU, then overwrite first half through the GPU.
        VkBufferCopy copy = {.srcOffset = 0, .dstOffset = size / 2, .size = size / 2};
        vkCmdCopyBuffer(cmd, buffer, buffer, 1, &copy);
        VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = buffer, .size = VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 1, &barrier, 0, NULL);
        uint32_t gpu_value = 0xaabb0000u + pass;
        vkCmdFillBuffer(cmd, buffer, 0, size / 2, gpu_value);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, NULL, 1, &barrier, 0, NULL);
        CHECK(vkEndCommandBuffer(cmd));
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1, .pCommandBuffers = &cmd};
        CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        CHECK(vkQueueWaitIdle(queue));
        for (size_t i = 0; i < size / 4; ++i)
            REQUIRE(guest[i] == (i < size / 8 ? gpu_value : cpu_value));
        runtime_memory_gpu_protect((uintptr_t)guest, size, 1, 1);
    }
    puts("PASS: three CPU/GPU round trips through shared Windows views; guest read protection preserved.");
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);
    REQUIRE(release((uint64_t)phys, allocation_size) == 0);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
