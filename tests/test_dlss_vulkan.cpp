// Explicit NGX creation/evaluation/readback test. Requires NVIDIA RTX + DLSS model.
// Ordinary CTests exercise settings/fallback without requiring this GPU.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include "gpu_release_headers.h"
#include "bbport_settings.h"
#include "video_core/renderer_vulkan/vk_dlss.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_staging_buffer_pool.h"
#include "video_core/texture_cache/image.h"

extern "C" void runtime_restart(void) { assert(false && "unexpected restart in DLSS test"); }

struct InputImage {
    VideoCore::UniqueImage image;
    vk::UniqueImageView view;
    Vulkan::Fsr4Upscaler::Image description;
    vk::ImageAspectFlags aspect;

    InputImage(const Vulkan::Instance& instance, vk::Format format, u32 w, u32 h, bool depth = false)
        : image{instance.GetDevice(), instance.GetAllocator()},
          aspect{depth ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor} {
        auto usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc;
        if (!depth) usage |= vk::ImageUsageFlagBits::eStorage;
        image.Create({.imageType = vk::ImageType::e2D, .format = format, .extent = {w, h, 1},
                      .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
                      .tiling = vk::ImageTiling::eOptimal, .usage = usage,
                      .initialLayout = vk::ImageLayout::eUndefined});
        auto created = instance.GetDevice().createImageViewUnique({
            .image = vk::Image(image), .viewType = vk::ImageViewType::e2D, .format = format,
            .subresourceRange = {aspect, 0, 1, 0, 1}});
        assert(created.result == vk::Result::eSuccess);
        view = std::move(created.value);
        description = {vk::Image(image), *view, w, h, format};
    }

    void Barrier(vk::CommandBuffer cmd, vk::ImageLayout old_layout, vk::ImageLayout layout) {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead,
            .oldLayout = old_layout, .newLayout = layout, .image = vk::Image(image),
            .subresourceRange = {aspect, 0, 1, 0, 1}};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
};

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e) -> LONG {
        std::fprintf(stderr, "DLSS test fault: code=%lx\n", e->ExceptionRecord->ExceptionCode);
        std::fprintf(stderr, "fault address=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx rax=%llx rbx=%llx rdi=%llx rsi=%llx\n",
            static_cast<unsigned long long>(e->ExceptionRecord->ExceptionInformation[1]),
            static_cast<unsigned long long>(e->ContextRecord->Rcx),static_cast<unsigned long long>(e->ContextRecord->Rdx),
            static_cast<unsigned long long>(e->ContextRecord->R8),static_cast<unsigned long long>(e->ContextRecord->R9),
            static_cast<unsigned long long>(e->ContextRecord->Rax),static_cast<unsigned long long>(e->ContextRecord->Rbx),
            static_cast<unsigned long long>(e->ContextRecord->Rdi),static_cast<unsigned long long>(e->ContextRecord->Rsi));
        CONTEXT walk = *e->ContextRecord;
        for (unsigned depth = 0; depth < 40 && walk.Rip; ++depth) {
            HMODULE module = nullptr;
            char path[MAX_PATH] = "?";
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<const char*>(walk.Rip), &module);
            if (module) GetModuleFileNameA(module, path, sizeof(path));
            std::fprintf(stderr, "  %s+%llx\n", path, static_cast<unsigned long long>(walk.Rip - reinterpret_cast<uintptr_t>(module)));
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
    Vulkan::Instance instance(static_cast<s32>(0), false);
    Vulkan::Scheduler scheduler(instance, true);
    Vulkan::StagingBufferPool staging(instance, scheduler);
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        Vulkan::DlssUpscaler dlss(instance, scheduler);
        if (!dlss.Available()) {
            std::printf("DLSS test unavailable: %s\n", dlss.Problem());
            return 77;
        }
        for (const auto [w, h] : {std::pair{640u, 360u}, std::pair{1280u, 720u}}) {
            const u32 ow = w * 3 / 2, oh = h * 3 / 2;
            InputImage color(instance, vk::Format::eR16G16B16A16Sfloat, w, h);
            InputImage depth(instance, vk::Format::eD32Sfloat, w, h, true);
            InputImage motion(instance, vk::Format::eR16G16Sfloat, w, h);
            InputImage output(instance, vk::Format::eR16G16B16A16Sfloat, ow, oh);
            auto cmd = scheduler.CommandBuffer();
            for (auto* image : {&color, &depth, &motion, &output})
                image->Barrier(cmd, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            cmd.clearColorImage(color.description.image, vk::ImageLayout::eGeneral,
                                vk::ClearColorValue{std::array{0.25f, 0.5f, 0.75f, 1.0f}},
                                {{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            cmd.clearDepthStencilImage(depth.description.image, vk::ImageLayout::eGeneral,
                                       {1.0f, 0}, {{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}});
            cmd.clearColorImage(motion.description.image, vk::ImageLayout::eGeneral,
                                vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}},
                                {{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            const float nan = std::numeric_limits<float>::quiet_NaN();
            cmd.clearColorImage(output.description.image, vk::ImageLayout::eGeneral,
                                vk::ClearColorValue{std::array{nan, nan, nan, nan}},
                                {{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            for (auto* image : {&color, &depth, &motion, &output})
                image->Barrier(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral);
            for (unsigned frame = 0; frame < 3; ++frame) {
                const bool ok = dlss.Record({
                    .cmdbuf = scheduler.CommandBuffer(), .color = color.description,
                    .depth = depth.description, .motion = motion.description, .output = output.description,
                    .render_width = w, .render_height = h, .preset = BbSettings::Quality,
                    .jitter = {}, .frame_ms = 16.67f, .near_plane = 0.1f, .far_plane = 1000.0f,
                    .vertical_fov = 1.0f, .sharpness = 0.0f, .sharpen = false,
                    .reset = frame == 0, .auto_exposure = true});
                if (!ok) {
                    std::printf("DLSS evaluation failed: %s\n", dlss.Problem());
                    scheduler.Finish();
                    dlss.Reset();
                    return 1;
                }
                scheduler.Finish();
            }
            const auto readback = staging.Request(ow * oh * 8ull, VideoCore::MemoryType::HostCached, 16);
            cmd = scheduler.CommandBuffer();
            output.Barrier(cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
            const vk::BufferImageCopy copy{.bufferOffset = readback.offset,
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, .imageExtent = {ow, oh, 1}};
            cmd.copyImageToBuffer(output.description.image, vk::ImageLayout::eTransferSrcOptimal,
                                  readback.buffer->Handle(), copy);
            scheduler.Finish();
            readback.Invalidate();
            // Poisoned output must have been replaced with finite, nonzero RGB samples.
            for (const u32 pixel : {ow * oh / 4, ow * oh / 2, ow * oh * 3 / 4}) {
                std::array<u16, 4> channels;
                std::memcpy(channels.data(), readback.mapped + pixel * 8ull, sizeof(channels));
                for (unsigned channel = 0; channel < 3; ++channel) {
                    assert((channels[channel] & 0x7c00) != 0x7c00);
                    assert((channels[channel] & 0x7fff) != 0);
                }
            }
            dlss.Reset(); // release before these input/output views leave scope
            std::printf("DLSS test: %ux%u -> %ux%u, three evaluations + finite RGB readback passed\n", w, h, ow, oh);
        }
        std::puts("DLSS test cycle passed, including feature recreation after resize.");
    }
    std::puts("DLSS test passed, including NGX shutdown/reinitialization.");
}
