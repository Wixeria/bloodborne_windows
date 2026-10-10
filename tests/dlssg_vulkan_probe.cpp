// Independent, opt-in Streamline/Vulkan probe. Never links into bb-game.
// Use tools/windows/test-dlssg.ps1; third-party DLLs stay in its private directory.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <vector>
#include "sl.h"
#include "sl_dlss_g.h"
#include "sl_reflex.h"
#include "sl_pcl.h"

static void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (Win32=%lu SDL=%s)\n", message,
                     GetLastError(), SDL_GetError());
        std::exit(1);
    }
}
static bool result(sl::Result value, const char* operation) {
    std::printf("%s: sl::Result=%d\n", operation, static_cast<int>(value));
    return value == sl::Result::eOk;
}
static void log_message(sl::LogType type, const char* message) {
    if (type != sl::LogType::eInfo || std::string(message).find("interpolation") != std::string::npos ||
        std::string(message).find("VSync") != std::string::npos)
        std::printf("Streamline[%d] %s\n", static_cast<int>(type), message);
}
template<class T> static T load(HMODULE module, const char* name) {
    auto fn = reinterpret_cast<T>(GetProcAddress(module, name));
    require(fn != nullptr, name);
    return fn;
}

static bool present_probe(VkInstance instance, VkPhysicalDevice physical, SDL_Window* window,
                          HMODULE module, PFN_vkGetInstanceProcAddr instance_proc,
                          bool generate);

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    require(argc >= 2 && argc <= 5,
            "usage: dlssg-vulkan-probe <private DLL directory> [--mod] [--present] [--fg-off]");
    bool use_mod = false, present = false, generate = true;
    for (int i = 2; i < argc; ++i) {
        const std::string flag(argv[i]);
        if (flag == "--mod") use_mod = true;
        else if (flag == "--present") present = true;
        else if (flag == "--fg-off") generate = false;
        else require(false, "unknown argument");
    }
    const auto directory = std::filesystem::absolute(argv[1]);
    std::filesystem::current_path(directory);
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(AddDllDirectory(directory.c_str()) != nullptr, "AddDllDirectory");
    // The proxy must initialize before Streamline queries architecture/capabilities.
    HMODULE mod = nullptr;
    if (use_mod) {
        mod = LoadLibraryExW((directory / "version.dll").c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(mod != nullptr, "load experimental SM86 proxy");
        std::puts("Experimental SM86 proxy loaded; this is not official RTX 30 support.");
    }
    auto module = LoadLibraryExW((directory / "sl.interposer.dll").c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    require(module != nullptr, "load Streamline interposer");
    auto init = load<PFun_slInit*>(module, "slInit");
    auto shutdown = load<PFun_slShutdown*>(module, "slShutdown");
    auto supported = load<PFun_slIsFeatureSupported*>(module, "slIsFeatureSupported");
    auto requirements = load<PFun_slGetFeatureRequirements*>(module, "slGetFeatureRequirements");
    auto feature_loaded = load<PFun_slIsFeatureLoaded*>(module, "slIsFeatureLoaded");
    auto instance_proc = load<PFN_vkGetInstanceProcAddr>(module, "vkGetInstanceProcAddr");
    const sl::Feature features[]{sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    const auto log_path = (directory / "logs").wstring();
    const auto dll_path = directory.wstring();
    const wchar_t* paths[]{dll_path.c_str()};
    sl::Preferences preferences{};
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                        sl::PreferenceFlags::eUseManualHooking |
                        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = 3;
    preferences.pathsToPlugins = paths;
    preferences.numPathsToPlugins = 1;
    preferences.pathToLogsAndData = log_path.c_str();
    preferences.logLevel = sl::LogLevel::eVerbose;
    preferences.logMessageCallback = log_message;
    // A local diagnostic project identity, not a registered production application.
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "bbport-probe-1";
    preferences.projectId = "c0e92b82-0468-4aad-a2fe-4b873322181b";
    if (!result(init(preferences, sl::kSDKVersion), "slInit")) return 1;

    require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization");
    auto window = SDL_CreateWindow("bbport DLSS frame generation probe", 1280, 720,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    require(window != nullptr, "SDL Vulkan window");
    uint32_t extension_count = 0;
    auto extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    require(extensions != nullptr, "SDL Vulkan extensions");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "bbport DLSSG diagnostic";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = extension_count;
    info.ppEnabledExtensionNames = extensions;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(instance_proc(nullptr, "vkCreateInstance"));
    require(create != nullptr, "Streamline vkCreateInstance");
    VkInstance instance{};
    require(create(&info, nullptr, &instance) == VK_SUCCESS, "Vulkan instance creation");
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(instance_proc(instance, "vkEnumeratePhysicalDevices"));
    auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(instance_proc(instance, "vkGetPhysicalDeviceProperties"));
    auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(instance_proc(instance, "vkDestroyInstance"));
    require(enumerate && properties && destroy, "Vulkan instance functions");
    uint32_t count = 0;
    require(enumerate(instance, &count, nullptr) == VK_SUCCESS && count, "Vulkan physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    require(enumerate(instance, &count, devices.data()) == VK_SUCCESS, "enumerate Vulkan devices");
    bool fg_supported = false;
    VkPhysicalDevice test_gpu{};
    for (auto physical : devices) {
        VkPhysicalDeviceProperties props{};
        properties(physical, &props);
        std::printf("GPU: %s vendor=%04x device=%04x\n", props.deviceName, props.vendorID, props.deviceID);
        sl::AdapterInfo adapter{};
        adapter.vkPhysicalDevice = physical;
        for (auto feature : features) {
            bool loaded = false;
            result(feature_loaded(feature, loaded), "slIsFeatureLoaded");
            std::printf("feature=%u loaded=%d\n", feature, loaded);
            const bool ok = result(supported(feature, adapter), "slIsFeatureSupported");
            // A proxy can spoof support even on Intel: only use the real NVIDIA adapter.
            if (feature == sl::kFeatureDLSS_G && ok && props.vendorID == 0x10de) {
                fg_supported = true;
                test_gpu = physical;
            }
            sl::FeatureRequirements req{};
            if (result(requirements(feature, req), "slGetFeatureRequirements")) {
                std::printf("requirements: flags=%u graphics=%u compute=%u optical-flow=%u device-extensions=%u\n",
                            static_cast<unsigned>(req.flags), req.vkNumGraphicsQueuesRequired,
                            req.vkNumComputeQueuesRequired, req.vkNumOpticalFlowQueuesRequired,
                            req.vkNumDeviceExtensions);
            }
        }
    }
    bool passed = true;
    if (present && fg_supported)
        passed = present_probe(instance, test_gpu, window, module, instance_proc, generate);
    // Streamline owns per-instance bookkeeping; shut down before destroying Vulkan.
    const bool stopped = present && fg_supported ? true : result(shutdown(), "slShutdown");
    destroy(instance, nullptr);
    SDL_DestroyWindow(window);
    SDL_Quit();
    FreeLibrary(module);
    if (mod) FreeLibrary(mod);
    std::printf("CAPABILITY_RESULT: %s\n",
                fg_supported ? "SUPPORTED" : "UNAVAILABLE");
    if (!present) std::puts("Capability check only; no generated frames have been verified.");
    return !stopped || !passed ? 1 : fg_supported ? 0 : 77;
}

// Conservative serialized fixture, deliberately not a performance benchmark.
#define DEVICE_FUNCTIONS(X) \
    X(vkGetDeviceQueue) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkQueueSubmit) X(vkDeviceWaitIdle) X(vkQueueWaitIdle) \
    X(vkDestroyDevice) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateImageView) \
    X(vkDestroyImageView) X(vkCmdPipelineBarrier) X(vkCmdClearColorImage) \
    X(vkCmdCopyImage) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCmdCopyBufferToImage)
struct DeviceApi {
#define FIELD(name) PFN_##name name{};
    DEVICE_FUNCTIONS(FIELD)
#undef FIELD
    DeviceApi(VkDevice device, PFN_vkGetDeviceProcAddr proc) {
#define LOAD(name) name = reinterpret_cast<PFN_##name>(proc(device, #name)); require(name != nullptr, #name);
        DEVICE_FUNCTIONS(LOAD)
#undef LOAD
    }
};
static void check(VkResult r, const char* call) {
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "FAIL: %s VkResult=%d\n", call, r);
        std::exit(1);
    }
}
#define VK_CHECK(call) check((call), #call)
#define SL_REQUIRE(call) require(result((call), #call), #call)
template<class T> static T feature_function(HMODULE module, sl::Feature feature, const char* name) {
    auto get = load<PFun_slGetFeatureFunction*>(module, "slGetFeatureFunction");
    void* function{};
    SL_REQUIRE(get(feature, name, function));
    require(function != nullptr, name);
    return reinterpret_cast<T>(function);
}
static uint32_t memory_type(const VkPhysicalDeviceMemoryProperties& properties,
                            uint32_t mask, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    require(false, "compatible Vulkan memory type");
    return 0;
}
struct ProbeImage {
    VkImage image{};
    VkImageView view{};
    VkDeviceMemory memory{};
    sl::Resource resource{};
    ProbeImage(DeviceApi& api, VkDevice device, const VkPhysicalDeviceMemoryProperties& props,
               VkFormat format, uint32_t width, uint32_t height) {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VK_CHECK(api.vkCreateImage(device, &info, nullptr, &image));
        VkMemoryRequirements requirements{};
        api.vkGetImageMemoryRequirements(device, image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(props, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(api.vkAllocateMemory(device, &allocation, nullptr, &memory));
        VK_CHECK(api.vkBindImageMemory(device, image, memory, 0));
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(api.vkCreateImageView(device, &view_info, nullptr, &view));
        resource = sl::Resource(sl::ResourceType::eTex2d, image, memory, view, VK_IMAGE_LAYOUT_GENERAL);
        resource.width = width; resource.height = height; resource.nativeFormat = format;
        resource.mipLevels = resource.arrayLayers = 1;
        resource.flags = 0; resource.usage = info.usage;
    }
    void destroy(DeviceApi& api, VkDevice device) {
        api.vkDestroyImageView(device, view, nullptr);
        api.vkDestroyImage(device, image, nullptr);
        api.vkFreeMemory(device, memory, nullptr);
    }
};
static void barrier(DeviceApi& api, VkCommandBuffer cmd, VkImage image,
                    VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    b.oldLayout = old_layout; b.newLayout = new_layout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    api.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
}

static bool present_probe(VkInstance instance, VkPhysicalDevice physical, SDL_Window* window,
                          HMODULE module, PFN_vkGetInstanceProcAddr proc, bool generate) {
#define INSTANCE_FUNCTION(name) auto name = reinterpret_cast<PFN_##name>(proc(instance, #name)); require(name != nullptr, #name);
    INSTANCE_FUNCTION(vkCreateWin32SurfaceKHR)
    INSTANCE_FUNCTION(vkDestroySurfaceKHR)
    INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties)
    INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR)
    INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR)
    INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR)
    INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties)
    INSTANCE_FUNCTION(vkCreateDevice)
#undef INSTANCE_FUNCTION
    auto device_proc = load<PFN_vkGetDeviceProcAddr>(module, "vkGetDeviceProcAddr");
    VkWin32SurfaceCreateInfoKHR surface_info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surface_info.hinstance = GetModuleHandleW(nullptr);
    surface_info.hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    require(surface_info.hwnd != nullptr, "SDL native HWND");
    VkSurfaceKHR surface{};
    VK_CHECK(vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface));
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 supported{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &supported));
        if (supported && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    }
    require(family != UINT32_MAX, "graphics + presentation queue");
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
    const char* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1; device_info.ppEnabledExtensionNames = extensions;
    // Proxy creates extra feature queues/extensions and informs Streamline of the device.
    VkDevice device{};
    VK_CHECK(vkCreateDevice(physical, &device_info, nullptr, &device));
    DeviceApi api(device, device_proc);
    VkQueue queue{};
    api.vkGetDeviceQueue(device, family, 0, &queue);
    auto fg_options = feature_function<PFun_slDLSSGSetOptions*>(module, sl::kFeatureDLSS_G, "slDLSSGSetOptions");
    auto fg_state = feature_function<PFun_slDLSSGGetState*>(module, sl::kFeatureDLSS_G, "slDLSSGGetState");
    auto reflex_options = feature_function<PFun_slReflexSetOptions*>(module, sl::kFeatureReflex, "slReflexSetOptions");
    auto reflex_sleep = feature_function<PFun_slReflexSleep*>(module, sl::kFeatureReflex, "slReflexSleep");
    auto marker = feature_function<PFun_slPCLSetMarker*>(module, sl::kFeaturePCL, "slPCLSetMarker");
    auto new_frame = load<PFun_slGetNewFrameToken*>(module, "slGetNewFrameToken");
    auto set_constants = load<PFun_slSetConstants*>(module, "slSetConstants");
    auto set_tags = load<PFun_slSetTagForFrame*>(module, "slSetTagForFrame");
    sl::ReflexOptions reflex{};
    reflex.mode = sl::ReflexMode::eLowLatency;
    reflex.frameLimitUs = 16667;
    SL_REQUIRE(reflex_options(reflex));
    const sl::ViewportHandle viewport(0);
    VkSurfaceCapabilitiesKHR caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    uint32_t formats_count{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formats_count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formats_count);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formats_count, formats.data()));
    const auto chosen = std::find_if(formats.begin(), formats.end(), [](auto f) {
        return f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    require(chosen != formats.end(), "SDR BGRA8 surface");
    const VkExtent2D extent = caps.currentExtent.width == UINT32_MAX ? VkExtent2D{1280,720} : caps.currentExtent;
    require(extent.width >= 720 && extent.height >= 720, "720p or larger output");
    require((caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) ==
            (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT), "swapchain image usage");
    const uint32_t w = extent.width, h = extent.height;
    uint32_t buffers = caps.minImageCount + 1;
    if (caps.maxImageCount && buffers > caps.maxImageCount) buffers = caps.maxImageCount;
    sl::DLSSGOptions options{};
    options.mode = generate ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = 1;
    options.numBackBuffers = buffers;
    options.mvecDepthWidth = options.colorWidth = w;
    options.mvecDepthHeight = options.colorHeight = h;
    options.colorBufferFormat = options.hudLessBufferFormat = chosen->format;
    options.mvecBufferFormat = VK_FORMAT_R32G32_SFLOAT;
    options.depthBufferFormat = VK_FORMAT_R32_SFLOAT;
    SL_REQUIRE(fg_options(viewport, options));
    VkSwapchainCreateInfoKHR swap_info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swap_info.surface = surface; swap_info.minImageCount = buffers;
    swap_info.imageFormat = chosen->format; swap_info.imageColorSpace = chosen->colorSpace;
    swap_info.imageExtent = extent; swap_info.imageArrayLayers = 1;
    swap_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swap_info.preTransform = caps.currentTransform;
    swap_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    uint32_t modes_count{};
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&modes_count,nullptr));
    std::vector<VkPresentModeKHR> modes(modes_count);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physical,surface,&modes_count,modes.data()));
    require(std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end(), "Immediate presentation support");
    // This driver reports no FG VSync support; FIFO disables interpolation after startup.
    swap_info.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR; swap_info.clipped = VK_TRUE;
    VkSwapchainKHR swapchain{};
    VK_CHECK(api.vkCreateSwapchainKHR(device, &swap_info, nullptr, &swapchain));
    VK_CHECK(api.vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr));
    std::vector<VkImage> images(count);
    VK_CHECK(api.vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()));
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    ProbeImage color(api, device, memory, chosen->format, w, h);
    ProbeImage depth(api, device, memory, VK_FORMAT_R32_SFLOAT, w, h);
    ProbeImage motion(api, device, memory, VK_FORMAT_R32G32_SFLOAT, w, h);
    const uint64_t pixels = uint64_t(w) * h;
    VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.size = pixels * 16; buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VkBuffer upload{};
    VK_CHECK(api.vkCreateBuffer(device, &buffer_info, nullptr, &upload));
    VkMemoryRequirements mr{};
    api.vkGetBufferMemoryRequirements(device, upload, &mr);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = mr.size;
    allocation.memoryTypeIndex = memory_type(memory, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory upload_memory{};
    VK_CHECK(api.vkAllocateMemory(device, &allocation, nullptr, &upload_memory));
    VK_CHECK(api.vkBindBufferMemory(device, upload, upload_memory, 0));
    void* mapped{};
    VK_CHECK(api.vkMapMemory(device, upload_memory, 0, VK_WHOLE_SIZE, 0, &mapped));
    auto rgba = static_cast<uint32_t*>(mapped);
    auto z = reinterpret_cast<float*>(rgba + pixels);
    auto mv = z + pixels;
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool_info.queueFamilyIndex = family;
    VkCommandPool pool{};
    VK_CHECK(api.vkCreateCommandPool(device, &pool_info, nullptr, &pool));
    VkCommandBufferAllocateInfo command_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command_info.commandPool = pool; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    VK_CHECK(api.vkAllocateCommandBuffers(device, &command_info, &cmd));
    VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore acquired{}, finished{};
    VK_CHECK(api.vkCreateSemaphore(device, &sem_info, nullptr, &acquired));
    VK_CHECK(api.vkCreateSemaphore(device, &sem_info, nullptr, &finished));
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    const auto focus_deadline = SDL_GetTicks() + 15000;
    while (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) && SDL_GetTicks() < focus_deadline) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0,
            "focus the probe window: NVIDIA disables FG for background windows");
    sl::DLSSGState state{};
    SL_REQUIRE(fg_state(viewport, state, nullptr));
    uint64_t presented = 0;
    uint64_t steady_presented = 0;
    uint32_t statuses = 0;
    constexpr uint32_t frames = 90;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        SDL_Delay(20); // Give the presentation worker time; this fixture is not a throughput test.
        SDL_Event event{};
        while (SDL_PollEvent(&event)) require(event.type != SDL_EVENT_QUIT, "probe window closed before completion");
        require((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0,
                "probe lost focus; background-frame suppression invalidates this measurement");
        sl::FrameToken* token{};
        SL_REQUIRE(new_frame(token, &frame));
        SL_REQUIRE(reflex_sleep(*token));
        SL_REQUIRE(marker(sl::PCLMarker::eSimulationStart, *token));
        const int left = 150 + int(frame) * 4;
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) {
            const auto p = uint64_t(y) * w + x;
            const bool box = int(x) >= left && int(x) < left + 160 && y >= 240 && y < 400;
            rgba[p] = box ? 0xff30c0f0u : ((x/32 + y/32) % 2 ? 0xff202020u : 0xff505050u);
            z[p] = box ? 0.5f : 0.9f;
            mv[p*2] = box && frame ? -4.0f : 0.0f; mv[p*2+1] = 0.0f;
        }
        sl::Constants constants{};
        // An orthographic synthetic scene with no camera movement or jitter.
        for (auto m : {&constants.cameraViewToClip, &constants.clipToCameraView,
                       &constants.clipToLensClip, &constants.clipToPrevClip, &constants.prevClipToClip})
            for (unsigned i = 0; i < 4; ++i) m->setRow(i, {i==0?1.f:0.f,i==1?1.f:0.f,i==2?1.f:0.f,i==3?1.f:0.f});
        constants.jitterOffset = {0,0}; constants.mvecScale = {1.f/w,1.f/h};
        constants.cameraPinholeOffset = {0,0}; constants.cameraPos = {0,0,0};
        constants.cameraUp = {0,1,0}; constants.cameraRight = {1,0,0}; constants.cameraFwd = {0,0,1};
        constants.cameraNear = 0.1f; constants.cameraFar = 1000.f;
        constants.cameraFOV = 1.f; constants.cameraAspectRatio = float(w)/h;
        constants.depthInverted = sl::eFalse; constants.cameraMotionIncluded = sl::eTrue;
        constants.motionVectors3D = sl::eFalse; constants.reset = frame == 0 ? sl::eTrue : sl::eFalse;
        constants.orthographicProjection = sl::eTrue;
        SL_REQUIRE(set_constants(constants, *token, viewport));
        SL_REQUIRE(marker(sl::PCLMarker::eSimulationEnd, *token));
        uint32_t index{};
        VK_CHECK(api.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index));
        VK_CHECK(api.vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(api.vkBeginCommandBuffer(cmd, &begin));
        unsigned n = 0;
        for (auto image : {&color, &depth, &motion}) {
            barrier(api, cmd, image->image, frame ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy copy{};
            copy.bufferOffset = pixels * 4 * n++;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {w,h,1};
            api.vkCmdCopyBufferToImage(cmd, upload, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier(api, cmd, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        barrier(api, cmd, images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        barrier(api, cmd, color.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkImageCopy copy{};
        copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.extent = {w,h,1};
        api.vkCmdCopyImage(cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier(api, cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        barrier(api, cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        const sl::Extent whole{0,0,w,h};
        sl::ResourceTag tags[]{
            {&depth.resource,sl::kBufferTypeDepth,sl::eValidUntilPresent,&whole},
            {&motion.resource,sl::kBufferTypeMotionVectors,sl::eValidUntilPresent,&whole},
            {&color.resource,sl::kBufferTypeHUDLessColor,sl::eValidUntilPresent,&whole}};
        SL_REQUIRE(set_tags(*token, viewport, tags, 3, cmd));
        VK_CHECK(api.vkEndCommandBuffer(cmd));
        SL_REQUIRE(marker(sl::PCLMarker::eRenderSubmitStart, *token));
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired; submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &finished;
        VK_CHECK(api.vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        SL_REQUIRE(marker(sl::PCLMarker::eRenderSubmitEnd, *token));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &finished;
        present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &index;
        SL_REQUIRE(marker(sl::PCLMarker::ePresentStart, *token));
        VK_CHECK(api.vkQueuePresentKHR(queue, &present));
        SL_REQUIRE(marker(sl::PCLMarker::ePresentEnd, *token));
        // Default eBlockPresentingClientQueue serializes FG input consumption on this queue.
        // Avoid vkDeviceWaitIdle here: its SL hook flushes the asynchronous presenter.
        VK_CHECK(api.vkQueueWaitIdle(queue));
        SL_REQUIRE(fg_state(viewport, state, nullptr));
        presented += state.numFramesActuallyPresented;
        if (frame >= 30) steady_presented += state.numFramesActuallyPresented;
        statuses |= static_cast<uint32_t>(state.status);
        std::printf("FRAME %u: presented=%u status=%u max-generated=%u\n", frame,
                    state.numFramesActuallyPresented, static_cast<uint32_t>(state.status), state.numFramesToGenerateMax);
    }
    std::printf("PRESENT_RESULT: fg=%s submitted=%u presented=%llu steady-submitted=60 steady-presented=%llu status=%u\n",
                generate ? "on" : "off", frames, static_cast<unsigned long long>(presented),
                static_cast<unsigned long long>(steady_presented), statuses);
    // Untag before releasing resources and unregister the swapchain before device destruction.
    sl::FrameToken* token{};
    uint32_t last = frames;
    SL_REQUIRE(new_frame(token, &last));
    sl::ResourceTag empty[]{{nullptr,sl::kBufferTypeDepth,sl::eValidUntilPresent},
                           {nullptr,sl::kBufferTypeMotionVectors,sl::eValidUntilPresent},
                           {nullptr,sl::kBufferTypeHUDLessColor,sl::eValidUntilPresent}};
    SL_REQUIRE(set_tags(*token, viewport, empty, 3, nullptr));
    options.mode = sl::DLSSGMode::eOff;
    SL_REQUIRE(fg_options(viewport, options));
    VK_CHECK(api.vkDeviceWaitIdle(device));
    api.vkDestroySwapchainKHR(device, swapchain, nullptr);
    api.vkDestroySemaphore(device, acquired, nullptr); api.vkDestroySemaphore(device, finished, nullptr);
    api.vkDestroyCommandPool(device, pool, nullptr);
    color.destroy(api,device); depth.destroy(api,device); motion.destroy(api,device);
    api.vkUnmapMemory(device, upload_memory);
    api.vkDestroyBuffer(device, upload, nullptr); api.vkFreeMemory(device, upload_memory, nullptr);
    // Shutdown must happen while the device is still alive.
    SL_REQUIRE(load<PFun_slShutdown*>(module,"slShutdown")());
    api.vkDestroyDevice(device,nullptr);
    vkDestroySurfaceKHR(instance,surface,nullptr);
    return statuses == 0 && (generate ? steady_presented >= 114 : steady_presented == 60);
}
