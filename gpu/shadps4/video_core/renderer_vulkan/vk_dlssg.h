// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include <array>
#include <memory>

namespace Vulkan {
class Instance;
class Scheduler;
class CameraMotion;
class Swapchain;
struct DlssgInput;
struct DlssgFrame;

// Optional, explicitly enabled Windows experiment. No SDK or DLL is needed when
// disabled.
class Dlssg {
  public:
    static PFN_vkGetInstanceProcAddr Loader(PFN_vkGetInstanceProcAddr native);
    static void Shutdown(); // after swapchain/resources, before destruction of VkDevice
    static bool Available();
    static bool NeedsCapture();
    // Guest boundaries: pad sampling follows Sleep, all graphics batches in the
    // same submit-done epoch share one token. No pad/renderer lock is held by callers.
    static void GuestInputSample();
    static void GuestSubmit();
    static void GuestDone();
    static void WindowMessage(u32 message); // SDL Win32 loop, no guest/pad lock
    static std::shared_ptr<DlssgFrame> GuestFrame();
    struct ReflexControls {
        bool loaded{}, ready{}, supported{}, failed{}, reports{}, guest_pacing{};
        int applied{-1};
    };
    static ReflexControls GetReflexControls();
    static std::shared_ptr<DlssgFrame> RenderStart();
    static void WaitInput(const Instance&, const std::shared_ptr<DlssgInput>&);
    static bool UsesPresentationProxy();
    struct Fps {
        float rendered{}, presented{};
        bool active{}, focused{};
    };
    static Fps GetFps(); // present thread, measured by Streamline, never an
                         // assumed multiplier
    struct Controls {
        bool loaded{}, ready{}, failed{}, control_run{};
        u32 max_multiplier{1}, applied_multiplier{1};
        u32 output_width{}, output_height{};
    };
    static Controls GetControls(); // queried by the overlay on the present thread
    static std::shared_ptr<DlssgInput> Capture(const Instance&, Scheduler&, CameraMotion&,
                                               vk::CommandBuffer, vk::ImageView depth,
                                               vk::ImageView motion, u32 w, u32 h,
                                               std::array<float, 2> jitter, bool reset,
                                               const std::shared_ptr<DlssgFrame>& timing = {});
    struct Color {
        vk::Image image;
        vk::ImageView view;
        u32 width{}, height{};
    };
    static void CaptureScene(const Instance&, vk::CommandBuffer, const std::shared_ptr<DlssgInput>&,
                             vk::Image source, vk::Format, u32 width, u32 height);
    static Color Scene(const std::shared_ptr<DlssgInput>&, bool bgra = false);
    static Color HudlessTarget(const Instance&, const std::shared_ptr<DlssgInput>&, vk::Format,
                               u32 width, u32 height);
    // Apply the same final-window blit as the backbuffer. Equal sizes reuse the
    // already processed source; otherwise the output remains leased with input.
    static Color PresentHudless(const Instance&, vk::CommandBuffer,
                                const std::shared_ptr<DlssgInput>&, vk::Extent2D,
                                const vk::ImageBlit&);
    static bool Begin(const Instance&, Swapchain&, const std::shared_ptr<DlssgInput>&,
                      bool eligible, bool focused, const std::shared_ptr<DlssgFrame>& timing = {});
    static void Tag(const Instance&, vk::CommandBuffer, const std::shared_ptr<DlssgInput>&);
    static void SubmitEnd();
    static void PresentStart();
    static void PresentEnd(const Instance&, const std::shared_ptr<DlssgInput>&);
    static void Drain(const Instance&);
};
} // namespace Vulkan
