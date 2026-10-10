// Actual image upload, GPU clear and readback: no game/save files are needed.
// Run explicitly on a compatible Vulkan GPU; ordinary CTests do not require one.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "gpu_release_headers.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include "video_core/amdgpu/regs.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
extern "C" {
#include "runtime_memory.h"
}

// The game executable supplies this application hook. This headless test has
// no guest to restart; reaching the overlay's restart action would be a failure.
extern "C" void runtime_restart(void) { assert(false && "unexpected guest restart in GPU test"); }

struct Caches {
    VideoCore::PageManager tracker{nullptr};
    VideoCore::BufferCache buffer;
    VideoCore::TextureCache texture;
    Caches(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler, Vulkan::Runtime& runtime)
        : buffer{instance, scheduler, runtime, nullptr, texture, tracker},
          texture{instance, scheduler, runtime, nullptr, buffer, tracker} {}
};

static std::array<float, 256> read_image(Vulkan::Runtime& runtime, Vulkan::Scheduler& scheduler,
                                       VideoCore::Image& image) {
    const auto download = runtime.GetStagingPool().Request(1024, VideoCore::MemoryType::HostCached, 16);
    const vk::BufferImageCopy copy{
        .bufferOffset = download.offset,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageExtent = {16, 16, 1},
    };
    runtime.DownloadImage(&image, download.buffer, std::span{&copy, 1});
    scheduler.Finish();
    download.Invalidate();
    std::array<float, 256> result;
    std::memcpy(result.data(), download.mapped, sizeof(result));
    // Request's default allocation is reusable after its fence; only held
    // asynchronous allocations use FreeDeferred.
    return result;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
        std::fprintf(stderr, "GPU test fault: code=%lx exe+%llx address=%llx\n",
            e->ExceptionRecord->ExceptionCode,
            static_cast<unsigned long long>(e->ContextRecord->Rip -
                reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))),
            static_cast<unsigned long long>(e->ExceptionRecord->ExceptionInformation[1]));
        CONTEXT walk = *e->ContextRecord;
        for (unsigned depth = 0; depth < 12 && walk.Rip; ++depth) {
            HMODULE module = nullptr;
            char path[MAX_PATH] = "?";
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<const char*>(walk.Rip), &module);
            if (module) GetModuleFileNameA(module, path, sizeof(path));
            std::fprintf(stderr, "  %s+%llx\n", path,
                static_cast<unsigned long long>(walk.Rip - reinterpret_cast<uintptr_t>(module)));
            DWORD64 base = 0;
            auto entry = RtlLookupFunctionEntry(walk.Rip, &base, nullptr);
            if (!entry) break;
            void* data = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, walk.Rip, entry, &walk, &data, &frame, nullptr);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif
    using Flexible = int32_t ABI (*)(void**, uint64_t, int, int);
    using Unmap = int32_t ABI (*)(void*, uint64_t);
    auto flexible = reinterpret_cast<Flexible>(runtime_memory_resolve("IWIBBdTHit4#p#J"));
    auto unmap = reinterpret_cast<Unmap>(runtime_memory_resolve("cQke9UuBQOk#p#J"));
    assert(flexible && unmap);
    void* guest = nullptr;
    assert(flexible(&guest, 16384, 3, 0) == 0);
    const auto base = reinterpret_cast<uintptr_t>(guest);
    std::array<float, 256> cpu;
    cpu.fill(0.25f);
    assert(runtime_memory_write_backing(base + 256, cpu.data(), sizeof(cpu)) == 1);
    {
        Vulkan::Instance instance(static_cast<s32>(0), false);
        std::puts("GPU test: instance ready");
        Vulkan::Scheduler scheduler(instance, true);
        Vulkan::Runtime runtime(instance, scheduler);
        std::puts("GPU test: scheduler/runtime ready");
        Caches caches(instance, scheduler, runtime);
        std::puts("GPU test: caches ready");
        scheduler.SetSubmitCallback([&](Vulkan::SubmitInfo& info) {
            runtime.FlushBarriers();
            caches.buffer.SubmitPendingArenaBinds(info);
        });
        VideoCore::TextureCache::ImageDesc desc;
        desc.info.pixel_format = vk::Format::eR32Sfloat;
        desc.info.type = AmdGpu::ImageType::Color2D;
        desc.info.size = {16, 16, 1};
        desc.info.pitch = 16;
        desc.info.num_bits = 32;
        desc.info.guest_address = base + 256;
        desc.info.guest_size = sizeof(cpu);
        desc.info.mips_layout[0] = {sizeof(cpu), 16, 16, 0};
        const auto id = caches.texture.FindImage(desc);
        std::puts("GPU test: image registered");
        auto& image = caches.texture.GetImage(id);
        // Exercise refresh/invalidation directly; this fixture has no rasterizer
        // for page-fault callbacks, so guest writes use the backing view below.
        caches.texture.RefreshImage(image);
        std::puts("GPU test: image uploaded");
        assert(read_image(runtime, scheduler, image)[0] == 0.25f);

        // A GPU-written exposure-like value must survive unchanged guest data.
        runtime.ClearImage(&image, {{0,0},{1,1}},
                           vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{1,0,0,0}}});
        assert(read_image(runtime, scheduler, image)[0] == 1.0f);
        caches.texture.InvalidateMemory(image.info.guest_address, 4);
        caches.texture.RefreshImage(image);
        assert(read_image(runtime, scheduler, image)[0] == 1.0f);

        // An unrelated write in the same page must not replace GPU contents.
        caches.texture.InvalidateMemory(base + 1536, 1);
        caches.texture.RefreshImage(image);
        assert(read_image(runtime, scheduler, image)[0] == 1.0f);

        // Once the shared page is untracked, a later CPU write past the initial
        // 8x8 pixels still has to be detected by the MaybeCpuDirty hash.
        caches.texture.InvalidateMemory(base + 1536, 1);
        const float changed = 0.75f;
        assert(runtime_memory_write_backing(base + 256 + 255 * sizeof(float), &changed, sizeof(changed)) == 1);
        caches.texture.RefreshImage(image);
        const auto result = read_image(runtime, scheduler, image);
        assert(result.front() == 0.25f && result.back() == 0.75f);
        scheduler.SetSubmitCallback({});
        scheduler.Finish();
    }
    assert(unmap(guest, 16384) == 0);
    std::puts("Texture cache GPU readback: retained GPU writes and detected tail CPU writes");
}
