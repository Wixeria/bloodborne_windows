/* Independent Windows WSI smoke: hidden HWND, swapchain acquire/clear/present.
   Uses the same native-handle extraction as the game renderer. */
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include "gpu/shim/bbport_window_platform.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(call) do { VkResult r=(call); if (r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR) { \
    fprintf(stderr,"Vulkan WSI: %s returned %d\n",#call,r); exit(1); } } while (0)
#define REQUIRE(value,message) do { if (!(value)) { fprintf(stderr,"Vulkan WSI: %s (%s)\n",message,SDL_GetError()); exit(1); } } while (0)

/* Function declarations/load code generated from the calls below; no SDK DLL/import library. */
static PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
static PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
static PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
static PFN_vkCmdClearColorImage vkCmdClearColorImage;
static PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
static PFN_vkCreateCommandPool vkCreateCommandPool;
static PFN_vkCreateDevice vkCreateDevice;
static PFN_vkCreateSemaphore vkCreateSemaphore;
static PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
static PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
static PFN_vkDestroyCommandPool vkDestroyCommandPool;
static PFN_vkDestroyDevice vkDestroyDevice;
static PFN_vkDestroyInstance vkDestroyInstance;
static PFN_vkDestroySemaphore vkDestroySemaphore;
static PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;
static PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
static PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
static PFN_vkEndCommandBuffer vkEndCommandBuffer;
static PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
static PFN_vkGetDeviceQueue vkGetDeviceQueue;
static PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
static PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
static PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
static PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
static PFN_vkQueuePresentKHR vkQueuePresentKHR;
static PFN_vkQueueSubmit vkQueueSubmit;
static PFN_vkResetCommandBuffer vkResetCommandBuffer;

int main(void) {
    REQUIRE(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization failed");
    SDL_Window *window=SDL_CreateWindow("bbport Windows presentation test",320,240,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);
    REQUIRE(window,"hidden Vulkan window creation failed");
    void *display,*native;
    REQUIRE(bb_window_native_handles(window,&display,&native)==BB_WINDOW_WINDOWS,"Win32 native handles missing");
    PFN_vkGetInstanceProcAddr get_instance=(PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    REQUIRE(get_instance,"Vulkan loader unavailable");
    PFN_vkCreateInstance vkCreateInstance=(PFN_vkCreateInstance)get_instance(NULL,"vkCreateInstance");
    REQUIRE(vkCreateInstance,"vkCreateInstance missing");
    uint32_t extension_count=0; const char *const *extensions=SDL_Vulkan_GetInstanceExtensions(&extension_count);
    REQUIRE(extensions,"SDL Vulkan extensions unavailable");
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="bbport Windows WSI smoke",.apiVersion=VK_API_VERSION_1_0};
    VkInstanceCreateInfo create={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,
        .enabledExtensionCount=extension_count,.ppEnabledExtensionNames=extensions};
    VkInstance instance; CHECK(vkCreateInstance(&create,NULL,&instance));
    vkCreateDevice=(PFN_vkCreateDevice)get_instance(instance,"vkCreateDevice"); REQUIRE(vkCreateDevice,"vkCreateDevice missing");
    vkCreateWin32SurfaceKHR=(PFN_vkCreateWin32SurfaceKHR)get_instance(instance,"vkCreateWin32SurfaceKHR"); REQUIRE(vkCreateWin32SurfaceKHR,"vkCreateWin32SurfaceKHR missing");
    vkDestroyInstance=(PFN_vkDestroyInstance)get_instance(instance,"vkDestroyInstance"); REQUIRE(vkDestroyInstance,"vkDestroyInstance missing");
    vkDestroySurfaceKHR=(PFN_vkDestroySurfaceKHR)get_instance(instance,"vkDestroySurfaceKHR"); REQUIRE(vkDestroySurfaceKHR,"vkDestroySurfaceKHR missing");
    vkEnumeratePhysicalDevices=(PFN_vkEnumeratePhysicalDevices)get_instance(instance,"vkEnumeratePhysicalDevices"); REQUIRE(vkEnumeratePhysicalDevices,"vkEnumeratePhysicalDevices missing");
    vkGetPhysicalDeviceProperties=(PFN_vkGetPhysicalDeviceProperties)get_instance(instance,"vkGetPhysicalDeviceProperties"); REQUIRE(vkGetPhysicalDeviceProperties,"vkGetPhysicalDeviceProperties missing");
    vkGetPhysicalDeviceQueueFamilyProperties=(PFN_vkGetPhysicalDeviceQueueFamilyProperties)get_instance(instance,"vkGetPhysicalDeviceQueueFamilyProperties"); REQUIRE(vkGetPhysicalDeviceQueueFamilyProperties,"vkGetPhysicalDeviceQueueFamilyProperties missing");
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR=(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)get_instance(instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"); REQUIRE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR missing");
    vkGetPhysicalDeviceSurfaceFormatsKHR=(PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)get_instance(instance,"vkGetPhysicalDeviceSurfaceFormatsKHR"); REQUIRE(vkGetPhysicalDeviceSurfaceFormatsKHR,"vkGetPhysicalDeviceSurfaceFormatsKHR missing");
    vkGetPhysicalDeviceSurfaceSupportKHR=(PFN_vkGetPhysicalDeviceSurfaceSupportKHR)get_instance(instance,"vkGetPhysicalDeviceSurfaceSupportKHR"); REQUIRE(vkGetPhysicalDeviceSurfaceSupportKHR,"vkGetPhysicalDeviceSurfaceSupportKHR missing");
    PFN_vkGetDeviceProcAddr get_device=(PFN_vkGetDeviceProcAddr)get_instance(instance,"vkGetDeviceProcAddr"); REQUIRE(get_device,"vkGetDeviceProcAddr missing");
    VkWin32SurfaceCreateInfoKHR surface_create={.sType=VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,.hinstance=display,.hwnd=native};
    VkSurfaceKHR surface; CHECK(vkCreateWin32SurfaceKHR(instance,&surface_create,NULL,&surface));
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL)); REQUIRE(count,"no Vulkan device");
    VkPhysicalDevice *devices=calloc(count,sizeof(*devices)); REQUIRE(devices,"allocation failed");
    CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));
    VkPhysicalDevice physical=VK_NULL_HANDLE; uint32_t family=0;
    for (uint32_t d=0;d<count && !physical;++d) {
        uint32_t n=0; vkGetPhysicalDeviceQueueFamilyProperties(devices[d],&n,NULL);
        VkQueueFamilyProperties *queues=calloc(n,sizeof(*queues)); REQUIRE(queues,"allocation failed");
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d],&n,queues);
        for (uint32_t q=0;q<n;++q) {
            VkBool32 supported=0; CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(devices[d],q,surface,&supported));
            if (supported && queues[q].queueCount && (queues[q].queueFlags&VK_QUEUE_GRAPHICS_BIT)) { physical=devices[d]; family=q; break; }
        }
        free(queues);
    }
    free(devices); REQUIRE(physical,"no graphics queue supporting HWND presentation");
    float priority=1;
    VkDeviceQueueCreateInfo queue_create={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *device_extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_create={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue_create,
        .enabledExtensionCount=1,.ppEnabledExtensionNames=device_extensions};
    VkDevice device; CHECK(vkCreateDevice(physical,&device_create,NULL,&device));
    vkAcquireNextImageKHR=(PFN_vkAcquireNextImageKHR)get_device(device,"vkAcquireNextImageKHR"); REQUIRE(vkAcquireNextImageKHR,"vkAcquireNextImageKHR missing");
    vkAllocateCommandBuffers=(PFN_vkAllocateCommandBuffers)get_device(device,"vkAllocateCommandBuffers"); REQUIRE(vkAllocateCommandBuffers,"vkAllocateCommandBuffers missing");
    vkBeginCommandBuffer=(PFN_vkBeginCommandBuffer)get_device(device,"vkBeginCommandBuffer"); REQUIRE(vkBeginCommandBuffer,"vkBeginCommandBuffer missing");
    vkCmdClearColorImage=(PFN_vkCmdClearColorImage)get_device(device,"vkCmdClearColorImage"); REQUIRE(vkCmdClearColorImage,"vkCmdClearColorImage missing");
    vkCmdPipelineBarrier=(PFN_vkCmdPipelineBarrier)get_device(device,"vkCmdPipelineBarrier"); REQUIRE(vkCmdPipelineBarrier,"vkCmdPipelineBarrier missing");
    vkCreateCommandPool=(PFN_vkCreateCommandPool)get_device(device,"vkCreateCommandPool"); REQUIRE(vkCreateCommandPool,"vkCreateCommandPool missing");
    vkCreateSemaphore=(PFN_vkCreateSemaphore)get_device(device,"vkCreateSemaphore"); REQUIRE(vkCreateSemaphore,"vkCreateSemaphore missing");
    vkCreateSwapchainKHR=(PFN_vkCreateSwapchainKHR)get_device(device,"vkCreateSwapchainKHR"); REQUIRE(vkCreateSwapchainKHR,"vkCreateSwapchainKHR missing");
    vkDestroyCommandPool=(PFN_vkDestroyCommandPool)get_device(device,"vkDestroyCommandPool"); REQUIRE(vkDestroyCommandPool,"vkDestroyCommandPool missing");
    vkDestroyDevice=(PFN_vkDestroyDevice)get_device(device,"vkDestroyDevice"); REQUIRE(vkDestroyDevice,"vkDestroyDevice missing");
    vkDestroySemaphore=(PFN_vkDestroySemaphore)get_device(device,"vkDestroySemaphore"); REQUIRE(vkDestroySemaphore,"vkDestroySemaphore missing");
    vkDestroySwapchainKHR=(PFN_vkDestroySwapchainKHR)get_device(device,"vkDestroySwapchainKHR"); REQUIRE(vkDestroySwapchainKHR,"vkDestroySwapchainKHR missing");
    vkDeviceWaitIdle=(PFN_vkDeviceWaitIdle)get_device(device,"vkDeviceWaitIdle"); REQUIRE(vkDeviceWaitIdle,"vkDeviceWaitIdle missing");
    vkEndCommandBuffer=(PFN_vkEndCommandBuffer)get_device(device,"vkEndCommandBuffer"); REQUIRE(vkEndCommandBuffer,"vkEndCommandBuffer missing");
    vkGetDeviceQueue=(PFN_vkGetDeviceQueue)get_device(device,"vkGetDeviceQueue"); REQUIRE(vkGetDeviceQueue,"vkGetDeviceQueue missing");
    vkGetSwapchainImagesKHR=(PFN_vkGetSwapchainImagesKHR)get_device(device,"vkGetSwapchainImagesKHR"); REQUIRE(vkGetSwapchainImagesKHR,"vkGetSwapchainImagesKHR missing");
    vkQueuePresentKHR=(PFN_vkQueuePresentKHR)get_device(device,"vkQueuePresentKHR"); REQUIRE(vkQueuePresentKHR,"vkQueuePresentKHR missing");
    vkQueueSubmit=(PFN_vkQueueSubmit)get_device(device,"vkQueueSubmit"); REQUIRE(vkQueueSubmit,"vkQueueSubmit missing");
    vkResetCommandBuffer=(PFN_vkResetCommandBuffer)get_device(device,"vkResetCommandBuffer"); REQUIRE(vkResetCommandBuffer,"vkResetCommandBuffer missing");
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkSurfaceCapabilitiesKHR capabilities; CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&capabilities));
    REQUIRE(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT,"swapchain lacks transfer destination usage");
    uint32_t format_count=0; CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&format_count,NULL)); REQUIRE(format_count,"no surface formats");
    VkSurfaceFormatKHR *formats=calloc(format_count,sizeof(*formats)); REQUIRE(formats,"allocation failed");
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,&format_count,formats));
    VkSurfaceFormatKHR format=formats[0];
    if (format.format==VK_FORMAT_UNDEFINED) format.format=VK_FORMAT_B8G8R8A8_UNORM;
    free(formats);
    VkExtent2D extent=capabilities.currentExtent;
    if (extent.width==UINT32_MAX) {
        extent=(VkExtent2D){320,240};
        if (extent.width<capabilities.minImageExtent.width) extent.width=capabilities.minImageExtent.width;
        if (extent.height<capabilities.minImageExtent.height) extent.height=capabilities.minImageExtent.height;
        if (extent.width>capabilities.maxImageExtent.width) extent.width=capabilities.maxImageExtent.width;
        if (extent.height>capabilities.maxImageExtent.height) extent.height=capabilities.maxImageExtent.height;
    }
    REQUIRE(extent.width && extent.height,"hidden surface has zero extent");
    uint32_t image_count=capabilities.minImageCount+1;
    if (capabilities.maxImageCount && image_count>capabilities.maxImageCount) image_count=capabilities.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha&alpha)) alpha=(VkCompositeAlphaFlagBitsKHR)(capabilities.supportedCompositeAlpha&(~capabilities.supportedCompositeAlpha+1));
    VkSwapchainCreateInfoKHR swap_create={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=surface,.minImageCount=image_count,
        .imageFormat=format.format,.imageColorSpace=format.colorSpace,.imageExtent=extent,.imageArrayLayers=1,
        .imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=capabilities.currentTransform,
        .compositeAlpha=alpha,.presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
    VkSwapchainKHR swapchain; CHECK(vkCreateSwapchainKHR(device,&swap_create,NULL,&swapchain));
    CHECK(vkGetSwapchainImagesKHR(device,swapchain,&image_count,NULL));
    VkImage *images=calloc(image_count,sizeof(*images)); REQUIRE(images,"allocation failed");
    CHECK(vkGetSwapchainImagesKHR(device,swapchain,&image_count,images));
    VkCommandPoolCreateInfo pool_create={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VkCommandPool pool; CHECK(vkCreateCommandPool(device,&pool_create,NULL,&pool));
    VkCommandBufferAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer commands; CHECK(vkAllocateCommandBuffers(device,&allocate,&commands));
    VkSemaphoreCreateInfo semaphore_create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore acquired,finished; CHECK(vkCreateSemaphore(device,&semaphore_create,NULL,&acquired)); CHECK(vkCreateSemaphore(device,&semaphore_create,NULL,&finished));
    for (unsigned frame=0;frame<3;++frame) {
        SDL_PumpEvents(); uint32_t index;
        CHECK(vkAcquireNextImageKHR(device,swapchain,UINT64_C(5000000000),acquired,VK_NULL_HANDLE,&index));
        CHECK(vkResetCommandBuffer(commands,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        CHECK(vkBeginCommandBuffer(commands,&begin));
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[index],
            .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkClearColorValue color={.float32={0.1f,0.2f,(float)frame/3.0f,1.0f}};
        vkCmdClearColorImage(commands,images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&barrier.subresourceRange);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=0;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barrier);
        CHECK(vkEndCommandBuffer(commands));
        VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&acquired,.pWaitDstStageMask=&wait_stage,
            .commandBufferCount=1,.pCommandBuffers=&commands,.signalSemaphoreCount=1,.pSignalSemaphores=&finished};
        CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));
        VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&finished,
            .swapchainCount=1,.pSwapchains=&swapchain,.pImageIndices=&index};
        CHECK(vkQueuePresentKHR(queue,&present)); CHECK(vkDeviceWaitIdle(device));
    }
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical,&properties);
    printf("Vulkan WSI: %s; Win32 HWND + %ux%u swapchain + 3 acquired/cleared/presented frames PASS\n",properties.deviceName,extent.width,extent.height);
    vkDestroySemaphore(device,finished,NULL); vkDestroySemaphore(device,acquired,NULL);
    vkDestroyCommandPool(device,pool,NULL); free(images); vkDestroySwapchainKHR(device,swapchain,NULL);
    vkDestroyDevice(device,NULL); vkDestroySurfaceKHR(instance,surface,NULL); vkDestroyInstance(instance,NULL);
    SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
