#pragma once

// bbgpu uses NDEBUG even when tests keep assertions enabled. Vulkan-Hpp embeds
// debug-only fields in DispatchLoaderBase, so parse its headers in the library's
// mode, then restore assertions for the actual test checks.
#ifndef NDEBUG
#define NDEBUG
#include "video_core/renderer_vulkan/vk_common.h"
#undef NDEBUG
#include <cassert>
#else
#include "video_core/renderer_vulkan/vk_common.h"
#endif
