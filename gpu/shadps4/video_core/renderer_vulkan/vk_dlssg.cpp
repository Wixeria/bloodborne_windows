// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_dlssg.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32) && defined(BB_HAS_STREAMLINE)
#include "bbport_settings.h"
#include "sl.h"
#include "sl_dlss_g.h"
#include "sl_pcl.h"
#include "sl_reflex.h"
#include "video_core/host_shaders/dlssg_snapshot_comp.h"
#include "video_core/renderer_vulkan/vk_camera_motion.h"
#include "video_core/renderer_vulkan/vk_dlssg_camera.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "video_core/texture_cache/image.h"
#include <algorithm>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <softpub.h>
#include <vector>
#include <vk_mem_alloc.h>
#include <windows.h>
#include <wintrust.h>

namespace Vulkan {
struct DlssgFrame {
    sl::FrameToken* token{};
    u32 id{};
    u64 epoch{};
    bool guest{};
    std::atomic<bool> sim_ended{}, render_started{}, render_ended{}, presented{};
};
struct DlssgInput {
    std::shared_ptr<DlssgFrame> timing;
    std::mutex completion_mutex;
    vk::Semaphore completion{};
    u64 completion_value{};
    VideoCore::UniqueImage depth, motion;
    vk::UniqueImageView depth_view, motion_view;
    VideoCore::UniqueImage scene, hudless, hudless_output;
    vk::UniqueImageView scene_view, scene_bgra_view, hudless_view, hudless_output_view;
    bool scene_ready{}, hudless_ready{}, hudless_output_ready{};
    vk::Rect2D presentation_rect{};
    u32 width{}, height{};
    sl::Constants constants{};
    bool reset{};
};
namespace {
struct Context {
    HMODULE module{}, mod{};
    bool initialized{}, available{}, failed{}, active{}, generate{};
    bool control_run{}, caps_known{}, request_fg{};
    std::atomic<bool> reflex_ready{}, reflex_supported{}, reflex_failed{}, reflex_reports{};
    std::atomic<bool> guest_pacing{};
    std::atomic<int> reflex_applied{-1};
    PFun_slReflexSetOptions* reflex_options{};
    PFun_slReflexGetState* reflex_state{};
    std::mutex reflex_api_mutex, guest_mutex;
    std::mutex event_mutex;
    std::atomic<u32> stats_message{};
    std::atomic<bool> flash_controlled{};
    std::shared_ptr<DlssgFrame> input_marker_frame; // event_mutex; never held during Sleep
    u64 guest_epoch{}, sampled_epoch{~0ull};
    std::shared_ptr<DlssgFrame> guest_frame, present_frame;
    bool parallel{};
    u32 missing_completions{};
    bool timeline_reported{};
    u32 max_multiplier{1};
    std::atomic<bool> usable{false};
    PFun_slShutdown* shutdown{};
    PFun_slGetNewFrameToken* new_frame{};
    PFun_slSetConstants* constants{};
    PFun_slSetTagForFrame* tags{};
    PFun_slDLSSGSetOptions* options{};
    PFun_slDLSSGGetState* state{};
    PFun_slReflexSleep* sleep{};
    PFun_slPCLSetMarker* marker{};
    sl::FrameToken* token{};
    sl::DLSSGOptions opts{};
    sl::ViewportHandle viewport{0};
    u32 index{};
    std::mutex token_mutex;
    bool frame_active{}, tag_active{}, history_reset{true};
    vk::SwapchainKHR configured_swapchain{};
    vk::UniqueDescriptorSetLayout descriptor;
    vk::UniquePipelineLayout layout;
    vk::UniquePipeline pipeline;
    std::vector<std::shared_ptr<DlssgInput>> pool;
    std::mutex pool_mutex;
    std::wstring directory, logs;
    u64 submitted{}, presented{}, samples{}, sample_presented{};
    u64 unfocused_frames{}, hudless_frames{}, resized_hudless_frames{};
    vk::Rect2D tagged_rect{};
    bool focused{};
    float rendered_fps{}, presented_fps{};
    std::chrono::steady_clock::time_point sample_start{};
    VkDevice owner{};
};
Context ctx;
sl::Result new_token(sl::FrameToken*& token, u32* id = nullptr) {
    std::scoped_lock lock(ctx.token_mutex);
    if (id)
        *id = ctx.index;
    const auto result = ctx.new_frame(token, &ctx.index);
    ++ctx.index;
    if (result == sl::Result::eOk && !token) return sl::Result::eErrorInvalidParameter;
    return result;
}
void complete(const Instance& instance, const std::shared_ptr<DlssgInput>& input) {
    if (!input)
        return;
    std::scoped_lock lock(input->completion_mutex);
    if (!input->completion)
        return;
    const vk::SemaphoreWaitInfo wait{.semaphoreCount = 1,
                                     .pSemaphores = &input->completion,
                                     .pValues = &input->completion_value};
    Check(instance.GetDevice().waitSemaphores(wait, std::numeric_limits<u64>::max()));
    input->completion = nullptr;
    input->completion_value = 0;
}
void complete_all(const Instance& instance) {
    std::scoped_lock lock(ctx.pool_mutex);
    for (const auto& input : ctx.pool)
        complete(instance, input);
}
bool ok(sl::Result r, const char* call) {
    if (r == sl::Result::eOk)
        return true;
    std::printf("DLSSG: %s failed (%d); disabling frame generation\n", call, int(r));
    ctx.failed = true;
    ctx.usable.store(false);
    return false;
}
template <class T> T symbol(const char* name) {
    return reinterpret_cast<T>(GetProcAddress(ctx.module, name));
}
template <class T> T feature(sl::Feature id, const char* name) {
    const auto get = symbol<PFun_slGetFeatureFunction*>("slGetFeatureFunction");
    void* address{};
    if (!get || !ok(get(id, name, address), name))
        return nullptr;
    return reinterpret_cast<T>(address);
}
void log(sl::LogType type, const char* message) {
    if (type != sl::LogType::eInfo)
        std::printf("DLSSG Streamline[%d]: %s\n", int(type), message);
}
bool signed_dll(const std::filesystem::path& path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const auto result = WinVerifyTrust(nullptr, &policy, &trust);
    auto provider = WTHelperProvDataFromStateData(trust.hWVTStateData);
    auto signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
    wchar_t name[256]{};
    if (signer && signer->csCertChain)
        CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                           name, 256);
    const bool nvidia = !std::wcscmp(name, L"NVIDIA Corporation");
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trust);
    return result == ERROR_SUCCESS && nvidia;
}
bool pinned_mod(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    bool success = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<char, 65536> buffer{};
    while (success && f) {
        f.read(buffer.data(), buffer.size());
        success = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(f.gcount()),
                                 0) >= 0;
    }
    std::array<UCHAR, 32> digest{};
    success = success && f.eof() && BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    std::string hex;
    constexpr char alphabet[] = "0123456789abcdef";
    for (auto b : digest) {
        hex += alphabet[b >> 4];
        hex += alphabet[b & 15];
    }
    return success && hex == "7489a89bf593cda243d95c62e6d8b75905d9774e27b7b2d53b91625a79c05ca4";
}
bool reflex_ok(sl::Result result, const char* call) {
    if (result == sl::Result::eOk)
        return true;
    if (!ctx.reflex_failed.exchange(true))
        std::printf("Reflex: %s failed (%d); guest pacing disabled\n", call, int(result));
    ctx.reflex_ready.store(false);
    ctx.usable.store(false);
    return false;
}
template <class T> T reflex_feature(sl::Feature id, const char* name) {
    void* address{};
    const auto get = symbol<PFun_slGetFeatureFunction*>("slGetFeatureFunction");
    return get && reflex_ok(get(id, name, address), name) ? reinterpret_cast<T>(address) : nullptr;
}
void trace(const std::shared_ptr<DlssgFrame>& frame, const char* stage) {
    static const bool enabled = [] {
        const char* value = std::getenv("BB_REFLEX_TRACE");
        return value && !std::strcmp(value, "1");
    }();
    if (!enabled || !frame)
        return;
    const auto clock = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    std::printf("Reflex marker: stage=%s token=%u epoch=%llu clock=%lld guest=%d\n", stage,
                frame->id, (unsigned long long)frame->epoch, (long long)clock, frame->guest);
}
void frame_mark(const std::shared_ptr<DlssgFrame>& frame, sl::PCLMarker marker, const char* stage) {
    if (frame && frame->token && ctx.reflex_ready.load() &&
        reflex_ok(ctx.marker(marker, *frame->token), "PCL marker"))
        trace(frame, stage);
}
void mark(sl::PCLMarker marker, const char* stage) {
    if (ctx.frame_active)
        frame_mark(ctx.present_frame, marker, stage);
}
bool apply_reflex() {
    int mode = std::clamp(BbSettings::Get().reflex.load(), 0, 2);
    // FG requires Reflex. Preserve the saved preference for the next FG-off frame.
    if (mode == 0 && ctx.usable.load() && BbSettings::Get().frame_generation.load() &&
        !ctx.control_run)
        mode = 1;
    if (ctx.reflex_applied.load() == mode)
        return true;
    std::scoped_lock lock(ctx.reflex_api_mutex); // SetOptions/GetState are not thread safe.
    sl::ReflexOptions options{};
    options.mode = static_cast<sl::ReflexMode>(mode);
    if (!reflex_ok(ctx.reflex_options(options), "options"))
        return false;
    ctx.reflex_applied.store(mode);
    std::printf("Reflex: applied=%s (guest input boundary, FG independent)\n", mode == 0 ? "off"
                                                                               : mode == 1
                                                                                   ? "on"
                                                                                   : "boost");
    return true;
}
void abort_frame(const Instance& instance) {
    std::scoped_lock submit_lock{Scheduler::submit_mutex};
    complete_all(instance);
    ctx.frame_active = false;
    ctx.tag_active = false;
    ctx.history_reset = true;
    if (ctx.options && ctx.active) {
        ctx.opts.mode = sl::DLSSGMode::eOff;
        ok(ctx.options(ctx.viewport, ctx.opts), "disable failed FG frame");
        ctx.active = false;
        ctx.configured_swapchain = nullptr;
    }
}
void create_pipeline(const Instance& instance) {
    const auto device = instance.GetDevice();
    std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};
    for (u32 n = 0; n < 4; ++n)
        bindings[n] = {.binding = n,
                       .descriptorType = n < 2 ? vk::DescriptorType::eSampledImage
                                               : vk::DescriptorType::eStorageImage,
                       .descriptorCount = 1,
                       .stageFlags = vk::ShaderStageFlagBits::eCompute};
    ctx.descriptor = Check(device.createDescriptorSetLayoutUnique(
        {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
         .bindingCount = 4,
         .pBindings = bindings.data()}));
    ctx.layout = Check(
        device.createPipelineLayoutUnique({.setLayoutCount = 1, .pSetLayouts = &*ctx.descriptor}));
    const auto shader = CompileSPV(DLSSG_SNAPSHOT_COMP, device);
    ctx.pipeline = Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{.stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                                                    .module = shader,
                                                    .pName = "main"},
                                          .layout = *ctx.layout}));
    device.destroyShaderModule(shader);
}
sl::Resource resource(const VideoCore::UniqueImage& image, vk::ImageView view, u32 w, u32 h) {
    VmaAllocationInfo allocation{};
    vmaGetAllocationInfo(image.allocator, image.allocation, &allocation);
    sl::Resource r(sl::ResourceType::eTex2d, VkImage(image.image), allocation.deviceMemory,
                   VkImageView(view), VK_IMAGE_LAYOUT_GENERAL);
    r.width = w;
    r.height = h;
    r.nativeFormat = uint32_t(image.image_ci.format);
    r.mipLevels = r.arrayLayers = 1;
    r.usage = uint32_t(image.image_ci.usage);
    return r;
}
} // namespace

PFN_vkGetInstanceProcAddr Dlssg::Loader(PFN_vkGetInstanceProcAddr native) {
    const char* mode = std::getenv("BB_FRAME_GENERATION");
    ctx.request_fg = mode && (!std::strcmp(mode, "on") || !std::strcmp(mode, "control") ||
                              !std::strcmp(mode, "menu"));
    if (!ctx.request_fg && !std::getenv("BB_STREAMLINE_DIR"))
        return native;
    if (ctx.initialized)
        return symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    ctx.control_run = mode && !std::strcmp(mode, "control");
    const char* parallel = std::getenv("BB_DLSSG_PARALLEL");
    ctx.parallel = parallel && !std::strcmp(parallel, "1");
    const char* directory = std::getenv("BB_STREAMLINE_DIR");
    if (!directory || !directory[0]) {
        std::puts("DLSSG: BB_STREAMLINE_DIR is required; using native Vulkan");
        return native;
    }
    const auto path = std::filesystem::absolute(std::filesystem::path(directory));
    ctx.directory = path.wstring();
    ctx.logs = (path / "logs").wstring();
    std::filesystem::create_directories(path / "logs");
    std::vector<const wchar_t*> dlls{L"sl.interposer.dll", L"sl.common.dll", L"sl.reflex.dll",
                                     L"sl.pcl.dll", L"NvLowLatencyVk.dll"};
    if (ctx.request_fg) {
        dlls.push_back(L"sl.dlss_g.dll");
        dlls.push_back(L"nvngx_dlssg.dll");
    }
    for (const auto* name : dlls) {
        if (!signed_dll(path / name)) {
            std::puts("DLSSG: NVIDIA runtime signature check failed; using native Vulkan");
            return native;
        }
    }
    if (const char* mod = std::getenv("BB_DLSSG_SM86");
        ctx.request_fg && mod && !std::strcmp(mod, "1")) {
        if (!pinned_mod(path / "version.dll")) {
            std::puts("DLSSG: experimental proxy hash mismatch");
            return native;
        }
        ctx.mod =
            LoadLibraryExW((path / "version.dll").c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!ctx.mod) {
            std::puts("DLSSG: experimental proxy load failed");
            return native;
        }
    }
    ctx.module =
        LoadLibraryExW((path / "sl.interposer.dll").c_str(), nullptr,
                       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!ctx.module) {
        std::puts("DLSSG: interposer load failed");
        return native;
    }
    const auto init = symbol<PFun_slInit*>("slInit");
    ctx.shutdown = symbol<PFun_slShutdown*>("slShutdown");
    const auto proc = symbol<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (!init || !ctx.shutdown || !proc)
        return native;
    std::vector<sl::Feature> features{sl::kFeatureReflex, sl::kFeaturePCL};
    if (ctx.request_fg)
        features.push_back(sl::kFeatureDLSS_G);
    const wchar_t* paths[]{ctx.directory.c_str()};
    sl::Preferences p{};
    p.renderAPI = sl::RenderAPI::eVulkan;
    p.flags = sl::PreferenceFlags::eDisableCLStateTracking |
              sl::PreferenceFlags::eUseManualHooking |
              sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    p.featuresToLoad = features.data();
    p.numFeaturesToLoad = u32(features.size());
    p.pathsToPlugins = paths;
    p.numPathsToPlugins = 1;
    p.pathToLogsAndData = ctx.logs.c_str();
    p.logLevel = sl::LogLevel::eDefault;
    p.logMessageCallback = log;
    p.engine = sl::EngineType::eCustom;
    p.engineVersion = "bbport-game-experiment-1";
    p.projectId = "c0e92b82-0468-4aad-a2fe-4b873322181b";
    if (!ok(init(p, sl::kSDKVersion), "slInit"))
        return native;
    ctx.initialized = true;
    std::printf("DLSSG: Streamline Vulkan game experiment initialized (%s, SM86=%d)\n",
                ctx.request_fg ? mode : "Reflex only", ctx.mod != nullptr);
    return proc;
}
bool Dlssg::Available() { return ctx.usable.load(); }
bool Dlssg::NeedsCapture() {
    return Available() && (ctx.control_run || BbSettings::Get().frame_generation.load());
}
void Dlssg::GuestInputSample() {
    if (!ctx.reflex_ready.load())
        return;
    std::scoped_lock lock(ctx.guest_mutex);
    if (!ctx.reflex_ready.load() || ctx.sampled_epoch == ctx.guest_epoch)
        return;
    if (!apply_reflex())
        return;
    auto frame = std::make_shared<DlssgFrame>();
    frame->guest = true;
    frame->epoch = ctx.guest_epoch;
    if (!reflex_ok(new_token(frame->token, &frame->id), "frame token") || !frame->token)
        return;
    trace(frame, "sleep_start");
    {
        std::scoped_lock event_lock(ctx.event_mutex);
        ctx.input_marker_frame = frame;
    }
    if (!reflex_ok(ctx.sleep(*frame->token), "sleep"))
        return;
    trace(frame, "sleep_end");
    frame_mark(frame, sl::PCLMarker::eSimulationStart, "simulation_start");
    frame_mark(frame, sl::PCLMarker::eControllerInputSample, "input_sample");
    ctx.guest_frame = frame;
    ctx.sampled_epoch = ctx.guest_epoch;
    ctx.guest_pacing.store(true);
}
void Dlssg::GuestSubmit() {
    if (!ctx.reflex_ready.load())
        return;
    std::scoped_lock lock(ctx.guest_mutex);
    const auto& frame = ctx.guest_frame;
    if (!frame || frame->epoch != ctx.guest_epoch)
        return;
    if (!frame->sim_ended.exchange(true))
        frame_mark(frame, sl::PCLMarker::eSimulationEnd, "simulation_end");
    if (!frame->render_started.exchange(true))
        frame_mark(frame, sl::PCLMarker::eRenderSubmitStart, "render_start");
}
void Dlssg::GuestDone() {
    std::scoped_lock lock(ctx.guest_mutex);
    ++ctx.guest_epoch;
}
void Dlssg::WindowMessage(u32 message) {
    if (!ctx.reflex_ready.load())
        return;
    const auto ping = ctx.stats_message.load();
    const bool flash = message == WM_LBUTTONDOWN && ctx.flash_controlled.load();
    if ((!ping || message != ping) && !flash)
        return;
    std::scoped_lock lock(ctx.event_mutex);
    if (!ctx.reflex_ready.load())
        return;
    // SDL input collection remains independent of Sleep on the guest thread.
    const auto frame = ctx.input_marker_frame;
    frame_mark(frame, flash ? sl::PCLMarker::eTriggerFlash : sl::PCLMarker::ePCLatencyPing,
               flash ? "flash" : "latency_ping");
}
std::shared_ptr<DlssgFrame> Dlssg::GuestFrame() {
    if (!ctx.reflex_ready.load())
        return {};
    std::scoped_lock lock(ctx.guest_mutex);
    return ctx.guest_frame && ctx.guest_frame->epoch == ctx.guest_epoch ? ctx.guest_frame : nullptr;
}
Dlssg::ReflexControls Dlssg::GetReflexControls() {
    return {ctx.initialized,          ctx.reflex_ready.load(),   ctx.reflex_supported.load(),
            ctx.reflex_failed.load(), ctx.reflex_reports.load(), ctx.guest_pacing.load(),
            ctx.reflex_applied.load()};
}
std::shared_ptr<DlssgFrame> Dlssg::RenderStart() {
    static const bool early = [] {
        const char* value = std::getenv("BB_DLSSG_EARLY_SLEEP");
        return !value || std::strcmp(value, "0");
    }();
    if (!early || !NeedsCapture() || ctx.guest_pacing.load())
        return {};
    auto frame = std::make_shared<DlssgFrame>();
    if (new_token(frame->token, &frame->id) != sl::Result::eOk || !frame->token ||
        ctx.sleep(*frame->token) != sl::Result::eOk) {
        std::puts("DLSSG: early renderer pacing failed; using presentation pacing");
        return {};
    }
    static bool reported = false;
    if (!std::exchange(reported, true))
        std::puts("DLSSG: Reflex pacing before scene draws; preserving the frame "
                  "token through present");
    return frame;
}
void Dlssg::WaitInput(const Instance& instance, const std::shared_ptr<DlssgInput>& input) {
    complete(instance, input);
}
bool Dlssg::UsesPresentationProxy() { return ctx.initialized && ctx.request_fg; }
Dlssg::Fps Dlssg::GetFps() {
    return {ctx.rendered_fps, ctx.presented_fps, ctx.active && ctx.generate && !ctx.failed,
            ctx.focused};
}
Dlssg::Controls Dlssg::GetControls() {
    return {ctx.initialized,     ctx.available && ctx.reflex_ready.load() && ctx.caps_known && ctx.max_multiplier >= 2,
            ctx.failed || ctx.reflex_failed.load(), ctx.control_run,
            ctx.max_multiplier,  ctx.active && ctx.generate ? ctx.opts.numFramesToGenerate + 1 : 1,
            ctx.opts.colorWidth, ctx.opts.colorHeight};
}

std::shared_ptr<DlssgInput> Dlssg::Capture(const Instance& instance, Scheduler& scheduler,
                                           CameraMotion& camera, vk::CommandBuffer cmd,
                                           vk::ImageView depth, vk::ImageView motion, u32 w, u32 h,
                                           std::array<float, 2> jitter, bool reset,
                                           const std::shared_ptr<DlssgFrame>& timing) {
    if (!NeedsCapture() || !camera.Ready())
        return {};
    std::scoped_lock lock(ctx.pool_mutex);
    if (!ctx.pipeline)
        create_pipeline(instance);
    std::shared_ptr<DlssgInput> input;
    for (auto& entry : ctx.pool)
        if (entry.use_count() == 1 && entry->width == w && entry->height == h) {
            input = entry;
            break;
        }
    if (!input) {
        std::erase_if(ctx.pool, [](const auto& entry) { return entry.use_count() == 1; });
        if (ctx.pool.size() >= 8) {
            std::puts("DLSSG: snapshot pool busy; skipping FG frame");
            return {};
        }
        input = std::make_shared<DlssgInput>();
        input->width = w;
        input->height = h;
        const auto device = instance.GetDevice();
        auto make = [&](VideoCore::UniqueImage& image, vk::UniqueImageView& view,
                        vk::Format format) {
            image = VideoCore::UniqueImage(device, instance.GetAllocator());
            image.Create(
                {.imageType = vk::ImageType::e2D,
                 .format = format,
                 .extent = {w, h, 1},
                 .mipLevels = 1,
                 .arrayLayers = 1,
                 .samples = vk::SampleCountFlagBits::e1,
                 .tiling = vk::ImageTiling::eOptimal,
                 .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled});
            view = Check(device.createImageViewUnique(
                {.image = image.image,
                 .viewType = vk::ImageViewType::e2D,
                 .format = format,
                 .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
        };
        make(input->depth, input->depth_view, vk::Format::eR32Sfloat);
        make(input->motion, input->motion_view, vk::Format::eR16G16Sfloat);
        ctx.pool.push_back(input);
    }
    input->constants = MakeDlssgConstants(camera.FrameGenerationCamera(), w, h, jitter, reset);
    input->reset = reset;
    input->timing = timing;
    input->scene_ready = input->hudless_ready = input->hudless_output_ready = false;
    input->presentation_rect = {};
    std::array<vk::ImageMemoryBarrier2, 2> barriers{};
    for (u32 n = 0; n < 2; ++n)
        barriers[n] = {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                       .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
                       .oldLayout = vk::ImageLayout::eUndefined,
                       .newLayout = vk::ImageLayout::eGeneral,
                       .image = n ? input->motion.image : input->depth.image,
                       .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    std::array<vk::DescriptorImageInfo, 4> images{
        {{.imageView = depth, .imageLayout = vk::ImageLayout::eGeneral},
         {.imageView = motion, .imageLayout = vk::ImageLayout::eGeneral},
         {.imageView = *input->depth_view, .imageLayout = vk::ImageLayout::eGeneral},
         {.imageView = *input->motion_view, .imageLayout = vk::ImageLayout::eGeneral}}};
    std::array<vk::WriteDescriptorSet, 4> writes{};
    for (u32 n = 0; n < 4; ++n)
        writes[n] = {.dstBinding = n,
                     .descriptorCount = 1,
                     .descriptorType = n < 2 ? vk::DescriptorType::eSampledImage
                                             : vk::DescriptorType::eStorageImage,
                     .pImageInfo = &images[n]};
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *ctx.pipeline);
    cmd.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *ctx.layout, 0, writes);
    cmd.dispatch((w + 7) / 8, (h + 7) / 8, 1);
    for (auto& b : barriers) {
        b.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
        b.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
        b.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        b.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        b.oldLayout = vk::ImageLayout::eGeneral;
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    // Even an unpresented/dropped snapshot stays alive until its draw submission
    // completes.
    scheduler.DeferOperation([input] {});
    return input;
}

void Dlssg::CaptureScene(const Instance& instance, vk::CommandBuffer cmd,
                         const std::shared_ptr<DlssgInput>& input, vk::Image source,
                         vk::Format format, u32 w, u32 h) {
    if (!input || (format != vk::Format::eR8G8B8A8Unorm && format != vk::Format::eR8G8B8A8Srgb))
        return;
    if (!input->scene || input->scene.image_ci.extent.width != w ||
        input->scene.image_ci.extent.height != h) {
        input->scene_view.reset();
        input->scene_bgra_view.reset();
        input->scene = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        input->scene.Create({.flags = vk::ImageCreateFlagBits::eMutableFormat,
                             .imageType = vk::ImageType::e2D,
                             .format = vk::Format::eR8G8B8A8Unorm,
                             .extent = {w, h, 1},
                             .mipLevels = 1,
                             .arrayLayers = 1,
                             .samples = vk::SampleCountFlagBits::e1,
                             .tiling = vk::ImageTiling::eOptimal,
                             .usage = vk::ImageUsageFlagBits::eTransferDst |
                                      vk::ImageUsageFlagBits::eTransferSrc |
                                      vk::ImageUsageFlagBits::eSampled});
        input->scene_view = Check(instance.GetDevice().createImageViewUnique(
            {.image = input->scene.image,
             .viewType = vk::ImageViewType::e2D,
             .format = vk::Format::eR8G8B8A8Srgb,
             .components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eOne},
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
        // The guest display pass reinterprets the scene's bytes as BGRA for
        // A8R8G8B8 output. Match that display interpretation without another copy.
        input->scene_bgra_view = Check(instance.GetDevice().createImageViewUnique(
            {.image = input->scene.image,
             .viewType = vk::ImageViewType::e2D,
             .format = vk::Format::eR8G8B8A8Srgb,
             .components = {vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eG,
                            vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eOne},
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    }
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    std::array<vk::ImageMemoryBarrier2, 2> barriers{
        {{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
          .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
          .oldLayout = vk::ImageLayout::eGeneral,
          .newLayout = vk::ImageLayout::eGeneral,
          .image = source,
          .subresourceRange = range},
         {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
          .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
          .oldLayout = vk::ImageLayout::eUndefined,
          .newLayout = vk::ImageLayout::eGeneral,
          .image = input->scene.image,
          .subresourceRange = range}}};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    const vk::ImageCopy copy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                             .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                             .extent = {w, h, 1}};
    cmd.copyImage(source, vk::ImageLayout::eGeneral, input->scene.image, vk::ImageLayout::eGeneral,
                  copy);
    for (u32 n = 0; n < 2; ++n) {
        auto& b = barriers[n];
        b.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        b.srcAccessMask =
            n ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eTransferRead;
        b.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        b.dstAccessMask = n ? vk::AccessFlagBits2::eShaderRead
                            : vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
        b.oldLayout = vk::ImageLayout::eGeneral;
        if (n)
            b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    input->scene_ready = true;
}
Dlssg::Color Dlssg::Scene(const std::shared_ptr<DlssgInput>& input, bool bgra) {
    if (!input || !input->scene_ready)
        return {};
    return {input->scene.image, bgra ? *input->scene_bgra_view : *input->scene_view,
            input->scene.image_ci.extent.width, input->scene.image_ci.extent.height};
}
Dlssg::Color Dlssg::HudlessTarget(const Instance& instance,
                                  const std::shared_ptr<DlssgInput>& input, vk::Format format,
                                  u32 w, u32 h) {
    if (!input || !input->scene_ready)
        return {};
    if (!input->hudless || input->hudless.image_ci.format != format ||
        input->hudless.image_ci.extent.width != w || input->hudless.image_ci.extent.height != h) {
        input->hudless_view.reset();
        input->hudless = VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        input->hudless.Create({.imageType = vk::ImageType::e2D,
                               .format = format,
                               .extent = {w, h, 1},
                               .mipLevels = 1,
                               .arrayLayers = 1,
                               .samples = vk::SampleCountFlagBits::e1,
                               .tiling = vk::ImageTiling::eOptimal,
                               .usage = vk::ImageUsageFlagBits::eColorAttachment |
                                        vk::ImageUsageFlagBits::eSampled |
                                        vk::ImageUsageFlagBits::eTransferSrc});
        input->hudless_view = Check(instance.GetDevice().createImageViewUnique(
            {.image = input->hudless.image,
             .viewType = vk::ImageViewType::e2D,
             .format = format,
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    }
    input->hudless_ready = true;
    return {input->hudless.image, *input->hudless_view, w, h};
}

Dlssg::Color Dlssg::PresentHudless(const Instance& instance, vk::CommandBuffer cmd,
                                   const std::shared_ptr<DlssgInput>& input, vk::Extent2D extent,
                                   const vk::ImageBlit& blit) {
    if (!input || !input->hudless_ready || !extent.width || !extent.height)
        return {};
    input->presentation_rect = {{blit.dstOffsets[0].x, blit.dstOffsets[0].y},
                                {u32(blit.dstOffsets[1].x - blit.dstOffsets[0].x),
                                 u32(blit.dstOffsets[1].y - blit.dstOffsets[0].y)}};
    const auto& source = input->hudless.image_ci;
    if (source.extent.width == extent.width && source.extent.height == extent.height)
        return {input->hudless.image, *input->hudless_view, extent.width, extent.height};
    if (!input->hudless_output || input->hudless_output.image_ci.format != source.format ||
        input->hudless_output.image_ci.extent.width != extent.width ||
        input->hudless_output.image_ci.extent.height != extent.height) {
        input->hudless_output_view.reset();
        input->hudless_output =
            VideoCore::UniqueImage(instance.GetDevice(), instance.GetAllocator());
        input->hudless_output.Create({.imageType = vk::ImageType::e2D,
                                      .format = source.format,
                                      .extent = {extent.width, extent.height, 1},
                                      .mipLevels = 1,
                                      .arrayLayers = 1,
                                      .samples = vk::SampleCountFlagBits::e1,
                                      .tiling = vk::ImageTiling::eOptimal,
                                      .usage = vk::ImageUsageFlagBits::eTransferDst |
                                               vk::ImageUsageFlagBits::eTransferSrc |
                                               vk::ImageUsageFlagBits::eSampled});
        input->hudless_output_view = Check(instance.GetDevice().createImageViewUnique(
            {.image = input->hudless_output.image,
             .viewType = vk::ImageViewType::e2D,
             .format = source.format,
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}}));
    }
    const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    std::array<vk::ImageMemoryBarrier2, 2> barriers{
        {{.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
          .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
          .oldLayout = vk::ImageLayout::eGeneral,
          .newLayout = vk::ImageLayout::eTransferSrcOptimal,
          .image = input->hudless.image,
          .subresourceRange = range},
         {.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
          .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
          .oldLayout = vk::ImageLayout::eUndefined,
          .newLayout = vk::ImageLayout::eTransferDstOptimal,
          .image = input->hudless_output.image,
          .subresourceRange = range}}};
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    // Match the final backbuffer's clear, letterboxing, format and linear filter,
    // rather than running a differently sized second host post-processing pass.
    const vk::ClearColorValue black{std::array<float, 4>{0, 0, 0, 1}};
    cmd.clearColorImage(input->hudless_output.image, vk::ImageLayout::eTransferDstOptimal, black,
                        range);
    const vk::MemoryBarrier2 clear_done{.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite};
    cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &clear_done});
    cmd.blitImage(input->hudless.image, vk::ImageLayout::eTransferSrcOptimal,
                  input->hudless_output.image, vk::ImageLayout::eTransferDstOptimal, blit,
                  vk::Filter::eLinear);
    for (u32 n = 0; n < barriers.size(); ++n) {
        auto& barrier = barriers[n];
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask =
            n ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eTransferRead;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
        barrier.oldLayout = barrier.newLayout;
        barrier.newLayout = vk::ImageLayout::eGeneral;
    }
    cmd.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = barriers.data()});
    input->hudless_output_ready = true;
    if (ctx.resized_hudless_frames++ == 0)
        std::printf("DLSSG: HUDless presentation blit %ux%u -> %ux%u\n", source.extent.width,
                    source.extent.height, extent.width, extent.height);
    return {input->hudless_output.image, *input->hudless_output_view, extent.width, extent.height};
}

bool Dlssg::Begin(const Instance& instance, Swapchain& swapchain,
                  const std::shared_ptr<DlssgInput>& input, bool eligible, bool focused,
                  const std::shared_ptr<DlssgFrame>& timing) {
    ctx.frame_active = false;
    ctx.tag_active = false;
    ctx.focused = focused;
    if (!ctx.initialized)
        return false;
    if (!ctx.owner) {
        ctx.owner = VkDevice(instance.GetDevice());
        sl::AdapterInfo adapter{};
        adapter.vkPhysicalDevice = VkPhysicalDevice(instance.GetPhysicalDevice());
        ctx.reflex_options =
            reflex_feature<PFun_slReflexSetOptions*>(sl::kFeatureReflex, "slReflexSetOptions");
        ctx.reflex_state =
            reflex_feature<PFun_slReflexGetState*>(sl::kFeatureReflex, "slReflexGetState");
        ctx.sleep = reflex_feature<PFun_slReflexSleep*>(sl::kFeatureReflex, "slReflexSleep");
        ctx.marker = reflex_feature<PFun_slPCLSetMarker*>(sl::kFeaturePCL, "slPCLSetMarker");
        ctx.new_frame = symbol<PFun_slGetNewFrameToken*>("slGetNewFrameToken");
        if (ctx.reflex_options && ctx.reflex_state && ctx.sleep && ctx.marker && ctx.new_frame &&
            apply_reflex()) {
            sl::ReflexState state{};
            if (reflex_ok(ctx.reflex_state(state), "initial state")) {
                ctx.reflex_supported.store(state.lowLatencyAvailable);
                ctx.reflex_reports.store(state.latencyReportAvailable);
                ctx.stats_message.store(state.statsWindowMessage);
                ctx.flash_controlled.store(state.flashIndicatorDriverControlled);
                ctx.reflex_ready.store(true);
                std::printf("Reflex: initialized supported=%d latency_reports=%d\n",
                            state.lowLatencyAvailable, state.latencyReportAvailable);
            }
        }
        auto supported = symbol<PFun_slIsFeatureSupported*>("slIsFeatureSupported");
        if (ctx.request_fg && supported &&
            supported(sl::kFeatureDLSS_G, adapter) == sl::Result::eOk) {
            ctx.options = feature<PFun_slDLSSGSetOptions*>(sl::kFeatureDLSS_G, "slDLSSGSetOptions");
            ctx.state = feature<PFun_slDLSSGGetState*>(sl::kFeatureDLSS_G, "slDLSSGGetState");
            ctx.constants = symbol<PFun_slSetConstants*>("slSetConstants");
            ctx.tags = symbol<PFun_slSetTagForFrame*>("slSetTagForFrame");
            ctx.available =
                ctx.options && ctx.state && ctx.constants && ctx.tags && ctx.reflex_ready.load();
            ctx.usable.store(ctx.available);
            if (ctx.available)
                std::puts("DLSSG: adapter supported, waiting for fresh gameplay depth/motion");
        } else if (ctx.request_fg) {
            std::puts("DLSSG: feature unsupported; Reflex remains independent");
        }
    }
    ctx.present_frame = input && input->timing ? input->timing : timing;
    if (ctx.present_frame && ctx.present_frame->presented.exchange(true))
        ctx.present_frame.reset();
    ctx.frame_active = ctx.present_frame && ctx.reflex_ready.load();
    ctx.token = ctx.present_frame ? ctx.present_frame->token : nullptr;
    if (!ctx.available)
        return ctx.frame_active;
    ctx.generate = BbSettings::Get().frame_generation.load() && !ctx.control_run;
    const u32 requested_multiplier = u32(BbSettings::Get().fg_multiplier.load());
    const u32 multiplier =
        ctx.caps_known ? std::clamp(requested_multiplier, 2u, std::max(2u, ctx.max_multiplier))
                       : 2u;
    const bool wanted = eligible && input &&
                        !ctx.failed && !swapchain.GetHDR() && swapchain.GetExtent().height >= 720 &&
                        swapchain.GetPresentMode() == vk::PresentModeKHR::eImmediate;
    const bool generating = wanted && ctx.generate && ctx.caps_known && ctx.max_multiplier >= 2;
    const auto hudless_format =
        input && input->hudless_ready ? uint32_t(input->hudless.image_ci.format) : 0u;
    const bool changed =
        ctx.active != generating || ctx.configured_swapchain != swapchain.GetHandle() ||
        ctx.opts.numFramesToGenerate != multiplier - 1 ||
        ctx.opts.hudLessBufferFormat != hudless_format ||
        (input &&
         (ctx.opts.mvecDepthWidth != input->width || ctx.opts.mvecDepthHeight != input->height));
    if (changed) {
        // SetOptions/recreation can call Vulkan queue/device idle internally.
        // Serialize them with all host submissions, including 0.4's workers.
        std::scoped_lock submit_lock{Scheduler::submit_mutex};
        // The plugin can replace its input-consumption timeline on mode/size
        // changes. Complete outstanding leases before changing options or
        // recreating the swapchain.
        complete_all(instance);
        ctx.opts.mode = generating ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
        ctx.opts.numFramesToGenerate = multiplier - 1;
        ctx.opts.numBackBuffers = swapchain.GetImageCount();
        ctx.opts.colorWidth = swapchain.GetExtent().width;
        ctx.opts.colorHeight = swapchain.GetExtent().height;
        ctx.opts.colorBufferFormat = uint32_t(swapchain.GetSurfaceFormat().format);
        ctx.opts.mvecBufferFormat = VK_FORMAT_R16G16_SFLOAT;
        ctx.opts.depthBufferFormat = VK_FORMAT_R32_SFLOAT;
        ctx.opts.mvecDepthWidth = input ? input->width : 0;
        ctx.opts.mvecDepthHeight = input ? input->height : 0;
        ctx.opts.hudLessBufferFormat = hudless_format;
        ctx.opts.queueParallelismMode =
            ctx.parallel ? sl::DLSSGQueueParallelismMode::eBlockNoClientQueues
                         : sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
        if (!ok(ctx.options(ctx.viewport, ctx.opts), "DLSSG options"))
            return false;
        if (ctx.active != generating) {
            Check(instance.GetPresentQueue().waitIdle());
            swapchain.Recreate(swapchain.GetWidth(), swapchain.GetHeight());
        }
        ctx.active = generating;
        ctx.configured_swapchain = swapchain.GetHandle();
        ctx.history_reset = true;
        ctx.rendered_fps = ctx.presented_fps = 0;
        sl::DLSSGState discard{};
        if (!ok(ctx.state(ctx.viewport, discard, nullptr), "reset presentation interval"))
            return false;
        ctx.max_multiplier = std::min(discard.numFramesToGenerateMax, 5u) + 1;
        ctx.caps_known = true;
        ctx.samples = ctx.sample_presented = 0;
        ctx.sample_start = std::chrono::steady_clock::now();
        std::printf("DLSSG: gameplay=%d generation=%d input=%ux%u output=%ux%u "
                    "(HUD interpolation experimental)\n",
                    wanted, generating, ctx.opts.mvecDepthWidth, ctx.opts.mvecDepthHeight,
                    ctx.opts.colorWidth, ctx.opts.colorHeight);
        std::printf("DLSSG: multiplier requested=%uX applied=%uX max=%uX\n", requested_multiplier,
                    generating ? multiplier : 1u, ctx.max_multiplier);
        std::printf("DLSSG: queue mode=%s (input leases synchronized before reuse)\n",
                    ctx.parallel ? "parallel" : "blocking");
        std::printf("DLSSG: eligibility frame=%d focused=%d input=%d token=%d guest_pacing=%d\n",
                    eligible, focused, bool(input), bool(ctx.present_frame), ctx.guest_pacing.load());
    }
    if (!wanted) {
        ctx.history_reset = true;
        return ctx.frame_active;
    }
    if (!ctx.present_frame) {
        // A real flip may have no fresh pad token (loading or multiple flips in
        // one guest epoch). Give that presentation its own non-guest token.
        // Never reuse a token or recreate the FG feature solely for that gap.
        // This fallback does not claim to measure guest simulation/input latency.
        ctx.present_frame = std::make_shared<DlssgFrame>();
        if (!reflex_ok(new_token(ctx.present_frame->token, &ctx.present_frame->id),
                       "fallback token"))
            return false;
        ctx.token = ctx.present_frame->token;
        ctx.frame_active = true;
        if (ctx.guest_pacing.load()) {
            static bool reported = false;
            if (!std::exchange(reported, true))
                std::puts("DLSSG: fresh presentation token for flip without unused guest token "
                          "(guest latency coverage unavailable for this flip)");
        }
        if (!ctx.guest_pacing.load() && !input->timing &&
            !reflex_ok(ctx.sleep(*ctx.token), "fallback sleep"))
            return false;
    }
    if (!ctx.present_frame->guest) {
        mark(sl::PCLMarker::eSimulationStart, "simulation_start");
        mark(sl::PCLMarker::eSimulationEnd, "simulation_end");
        mark(sl::PCLMarker::eRenderSubmitStart, "render_start");
    }
    auto c = input->constants;
    c.reset = ctx.history_reset || input->reset ? sl::eTrue : sl::eFalse;
    if (!ok(ctx.constants(c, *ctx.token, ctx.viewport), "constants")) {
        abort_frame(instance);
        return false;
    }
    ctx.history_reset = false;
    ctx.tag_active = true;
    if (ctx.failed) {
        abort_frame(instance);
        return false;
    }
    return true;
}
void Dlssg::Tag(const Instance& instance, vk::CommandBuffer cmd,
                const std::shared_ptr<DlssgInput>& input) {
    if (!ctx.frame_active || !ctx.tag_active || !ctx.available || !input)
        return;
    auto d = resource(input->depth, *input->depth_view, input->width, input->height);
    auto m = resource(input->motion, *input->motion_view, input->width, input->height);
    const sl::Extent whole{0, 0, input->width, input->height};
    sl::Resource hudless;
    const sl::Extent output{0, 0, ctx.opts.colorWidth, ctx.opts.colorHeight};
    const auto& color = input->hudless_output_ready ? input->hudless_output : input->hudless;
    const auto& view =
        input->hudless_output_ready ? input->hudless_output_view : input->hudless_view;
    const bool clean = input->hudless_ready && color.image_ci.extent.width == output.width &&
                       color.image_ci.extent.height == output.height;
    const auto rect = input->presentation_rect;
    const sl::Extent region = rect.extent.width && rect.extent.height
                                  ? sl::Extent{.top = u32(rect.offset.y),
                                               .left = u32(rect.offset.x),
                                               .width = rect.extent.width,
                                               .height = rect.extent.height}
                                  : output;
    if (clean && (ctx.hudless_frames++ == 0 || ctx.tagged_rect != rect)) {
        std::printf("DLSSG: tagged tonemapped HUDless scene %ux%u\n", output.width, output.height);
        std::printf("DLSSG: presentation region %u,%u %ux%u\n", region.left, region.top,
                    region.width, region.height);
        ctx.tagged_rect = rect;
    }
    if (clean)
        hudless = resource(color, *view, output.width, output.height);
    sl::ResourceTag tags[]{{&d, sl::kBufferTypeDepth, sl::eValidUntilPresent, &whole},
                           {&m, sl::kBufferTypeMotionVectors, sl::eValidUntilPresent, &whole},
                           {clean ? &hudless : nullptr, sl::kBufferTypeHUDLessColor,
                            sl::eValidUntilPresent, clean ? &region : nullptr},
                           // A null resource is deliberate: Streamline already knows
                           // the presented image. Full extents also clear any previous
                           // letterbox crop when the window returns to 16:9.
                           {nullptr, sl::kBufferTypeBackbuffer, sl::ResourceLifecycle{}, &region}};
    if (!ok(ctx.tags(*ctx.token, ctx.viewport, tags, 4, VkCommandBuffer(cmd)), "resource tags")) {
        abort_frame(instance);
        return;
    }
}
void Dlssg::SubmitEnd() {
    if (ctx.frame_active && ctx.present_frame && !ctx.present_frame->render_ended.exchange(true))
        mark(sl::PCLMarker::eRenderSubmitEnd, "render_end");
}
void Dlssg::PresentStart() { mark(sl::PCLMarker::ePresentStart, "present_start"); }
void Dlssg::PresentEnd(const Instance& instance, const std::shared_ptr<DlssgInput>& input) {
    mark(sl::PCLMarker::ePresentEnd, "present_end");
    if (!ctx.frame_active)
        return;
    // State/option calls use a separate mutex; never hold guest_mutex on the
    // present thread while the CPU may be sleeping for this presentation.
    if (ctx.reflex_ready.load()) {
        static auto report_time = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - report_time >= std::chrono::seconds(2)) {
            std::scoped_lock lock(ctx.reflex_api_mutex);
            sl::ReflexState state{};
            if (reflex_ok(ctx.reflex_state(state), "state")) {
                ctx.reflex_supported.store(state.lowLatencyAvailable);
                ctx.reflex_reports.store(state.latencyReportAvailable);
                ctx.stats_message.store(state.statsWindowMessage);
                ctx.flash_controlled.store(state.flashIndicatorDriverControlled);
                u32 reports = 0;
                const sl::ReflexReport* newest = nullptr;
                for (const auto& report : state.frameReport) {
                    if (report.simStartTime && report.simEndTime >= report.simStartTime &&
                        report.renderSubmitStartTime >= report.simEndTime &&
                        report.renderSubmitEndTime >= report.renderSubmitStartTime &&
                        report.presentStartTime >= report.renderSubmitEndTime &&
                        report.presentEndTime >= report.presentStartTime &&
                        report.gpuRenderEndTime > report.gpuRenderStartTime) {
                        ++reports;
                        if (!newest || report.frameID > newest->frameID)
                            newest = &report;
                    }
                }
                if (state.latencyReportAvailable && newest)
                    std::printf(
                        "Reflex report: valid=%u token=%llu sim_start=%llu sim_end=%llu "
                        "submit_start=%llu submit_end=%llu present_start=%llu present_end=%llu "
                        "gpu_start=%llu gpu_end=%llu input=%llu gpu_active_us=%u\n",
                        reports, (unsigned long long)newest->frameID,
                        (unsigned long long)newest->simStartTime,
                        (unsigned long long)newest->simEndTime,
                        (unsigned long long)newest->renderSubmitStartTime,
                        (unsigned long long)newest->renderSubmitEndTime,
                        (unsigned long long)newest->presentStartTime,
                        (unsigned long long)newest->presentEndTime,
                        (unsigned long long)newest->gpuRenderStartTime,
                        (unsigned long long)newest->gpuRenderEndTime,
                        (unsigned long long)newest->inputSampleTime, newest->gpuActiveRenderTimeUs);
                std::printf("Reflex stats: mode=%d supported=%d latency_reports=%d guest_pacing=%d "
                            "focused=%d\n",
                            ctx.reflex_applied.load(), state.lowLatencyAvailable,
                            state.latencyReportAvailable, ctx.guest_pacing.load(), ctx.focused);
            }
            report_time = now;
        }
    }
    if (!ctx.available) {
        ctx.frame_active = false;
        return;
    }
    // Presenter signals the frame's reuse fence AFTER vkQueuePresentKHR. Default
    // block-presenting-queue mode orders that fence after FG input consumption.
    // A conservative queue-idle diagnostic remains opt-in for matched
    // comparisons.
    static const bool serialized = [] {
        const char* value = std::getenv("BB_DLSSG_SERIALIZE");
        return value && !std::strcmp(value, "1");
    }();
    if (serialized)
        Check(instance.GetPresentQueue().waitIdle());
    sl::DLSSGState s{};
    if (ok(ctx.state(ctx.viewport, s, nullptr), "DLSSG state")) {
        if (ctx.parallel && ctx.active && input) {
            if (s.inputsProcessingCompletionFence &&
                s.lastPresentInputsProcessingCompletionFenceValue) {
                if (!std::exchange(ctx.timeline_reported, true))
                    std::puts("DLSSG: Vulkan input completion timeline available");
                ctx.missing_completions = 0;
                std::scoped_lock lock(input->completion_mutex);
                input->completion =
                    vk::Semaphore(reinterpret_cast<VkSemaphore>(s.inputsProcessingCompletionFence));
                input->completion_value = s.lastPresentInputsProcessingCompletionFenceValue;
            } else {
                // Old providers may not expose a Vulkan completion timeline. Drain
                // all device queues before using the conservative mode next frame.
                Check(instance.GetDevice().waitIdle());
                // Startup presents may precede the first generated frame. Give the
                // provider a bounded initialization interval, with safe device drains.
                if (++ctx.missing_completions >= 8) {
                    ctx.parallel = false;
                    ctx.configured_swapchain = nullptr;
                    std::puts("DLSSG: input completion timeline unavailable; reverting "
                              "to blocking mode");
                }
            }
        }
        ++ctx.submitted;
        ++ctx.samples;
        ctx.presented += s.numFramesActuallyPresented;
        ctx.sample_presented += s.numFramesActuallyPresented;
        if (!ctx.focused)
            ++ctx.unfocused_frames;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - ctx.sample_start).count();
        if (seconds >= 2.0 || s.status != sl::DLSSGStatus::eOk) {
            ctx.rendered_fps = ctx.samples / seconds;
            ctx.presented_fps = ctx.sample_presented / seconds;
            std::printf("DLSSG stats: real=%.2f FPS presented=%.2f FPS submitted=%llu "
                        "presentations=%llu status=%u fg=%d focused=%d unfocused=%llu\n",
                        ctx.samples / seconds, ctx.sample_presented / seconds,
                        (unsigned long long)ctx.submitted, (unsigned long long)ctx.presented,
                        uint32_t(s.status), ctx.active, ctx.focused,
                        (unsigned long long)ctx.unfocused_frames);
            ctx.samples = ctx.sample_presented = 0;
            ctx.sample_start = now;
        }
        if (s.status != sl::DLSSGStatus::eOk) {
            ctx.failed = true;
            ctx.usable.store(false);
        }
    } else if (ctx.parallel && ctx.active) {
        // A failed state query leaves no usable completion value for this present.
        // Finish plugin reads before this frame can be returned to the reuse pool.
        Check(instance.GetDevice().waitIdle());
    }
    ctx.frame_active = false;
}
void Dlssg::Drain(const Instance& instance) {
    if (!ctx.initialized || !ctx.available)
        return;
    ctx.frame_active = false;
    ctx.history_reset = true;
    complete_all(instance);
    ctx.opts.mode = sl::DLSSGMode::eOff;
    ok(ctx.options(ctx.viewport, ctx.opts), "disable before recreation");
    ctx.active = false;
    Check(instance.GetPresentQueue().waitIdle());
}
void Dlssg::Shutdown() {
    ctx.reflex_ready.store(false);
    std::scoped_lock guest_lock(ctx.guest_mutex);
    std::scoped_lock event_lock(ctx.event_mutex);
    ctx.input_marker_frame.reset();
    ctx.guest_frame.reset();
    ctx.present_frame.reset();
    ctx.usable.store(false);
    if (ctx.initialized) {
        std::printf("DLSSG total: submitted=%llu presentations=%llu\n",
                    (unsigned long long)ctx.submitted, (unsigned long long)ctx.presented);
        ok(ctx.shutdown(), "slShutdown");
        ctx.initialized = false;
    }
    ctx.pool.clear();
    ctx.pipeline.reset();
    ctx.layout.reset();
    ctx.descriptor.reset();
    // Keep interposer/proxy loaded until process exit: Vulkan destruction still
    // uses their dispatch.
}
} // namespace Vulkan
#else
namespace Vulkan {
PFN_vkGetInstanceProcAddr Dlssg::Loader(PFN_vkGetInstanceProcAddr native) {
    if (const char* requested = std::getenv("BB_FRAME_GENERATION");
        requested && (!std::strcmp(requested, "on") || !std::strcmp(requested, "control") ||
                      !std::strcmp(requested, "menu")))
        std::puts("DLSSG: support is not compiled; rebuild with Streamline SDK headers");
    return native;
}
void Dlssg::Shutdown() {}
bool Dlssg::Available() { return false; }
bool Dlssg::NeedsCapture() { return false; }
void Dlssg::GuestInputSample() {}
void Dlssg::GuestSubmit() {}
void Dlssg::GuestDone() {}
void Dlssg::WindowMessage(u32) {}
std::shared_ptr<DlssgFrame> Dlssg::GuestFrame() { return {}; }
Dlssg::ReflexControls Dlssg::GetReflexControls() { return {}; }
std::shared_ptr<DlssgFrame> Dlssg::RenderStart() { return {}; }
void Dlssg::WaitInput(const Instance&, const std::shared_ptr<DlssgInput>&) {}
bool Dlssg::UsesPresentationProxy() { return false; }
Dlssg::Fps Dlssg::GetFps() { return {}; }
Dlssg::Controls Dlssg::GetControls() { return {}; }
std::shared_ptr<DlssgInput> Dlssg::Capture(const Instance&, Scheduler&, CameraMotion&,
                                           vk::CommandBuffer, vk::ImageView, vk::ImageView, u32,
                                           u32, std::array<float, 2>, bool,
                                           const std::shared_ptr<DlssgFrame>&) {
    return {};
}
void Dlssg::CaptureScene(const Instance&, vk::CommandBuffer, const std::shared_ptr<DlssgInput>&,
                         vk::Image, vk::Format, u32, u32) {}
Dlssg::Color Dlssg::Scene(const std::shared_ptr<DlssgInput>&, bool) { return {}; }
Dlssg::Color Dlssg::HudlessTarget(const Instance&, const std::shared_ptr<DlssgInput>&, vk::Format,
                                  u32, u32) {
    return {};
}
Dlssg::Color Dlssg::PresentHudless(const Instance&, vk::CommandBuffer,
                                   const std::shared_ptr<DlssgInput>&, vk::Extent2D,
                                   const vk::ImageBlit&) {
    return {};
}
bool Dlssg::Begin(const Instance&, Swapchain&, const std::shared_ptr<DlssgInput>&, bool, bool,
                  const std::shared_ptr<DlssgFrame>&) {
    return false;
}
void Dlssg::Tag(const Instance&, vk::CommandBuffer, const std::shared_ptr<DlssgInput>&) {}
void Dlssg::SubmitEnd() {}
void Dlssg::PresentStart() {}
void Dlssg::PresentEnd(const Instance&, const std::shared_ptr<DlssgInput>&) {}
void Dlssg::Drain(const Instance&) {}
} // namespace Vulkan
#endif
