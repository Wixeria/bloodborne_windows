// SPDX-License-Identifier: GPL-2.0-or-later
// Verify the game's column-vector camera -> Streamline row-vector conversion.
#include "video_core/renderer_vulkan/vk_dlssg_camera.h"
#include <cassert>
#include <cstdio>

static std::array<float, 4> transform_point(const sl::float4x4& m, std::array<float, 4> p) {
    std::array<float, 4> out{};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            out[j] += p[i] * (&m[i].x)[j];
    return out;
}
static void close(float a, float b) {
    if (std::abs(a - b) >= 2e-4f)
        std::printf("Mismatch: actual=%g expected=%g\n", a, b);
    assert(std::abs(a - b) < 2e-4f);
}
struct Camera {
    std::array<float, 12> inverse_view, previous_view;
    std::array<float, 4> projection, previous_projection;
};
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::array<float, 12> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const float near = .1f, far = 3000.f, z = far / (far - near), offset = -near * z;
    Camera camera{identity, identity, {1.2f, 1.8f, z, offset}, {1.2f, 1.8f, z, offset}};
    auto c = Vulkan::MakeDlssgConstants(camera, 1280, 720, {.25f, -.125f}, true);
    auto n = transform_point(c.cameraViewToClip, {0, 0, near, 1});
    close(n[2] / n[3], 0);
    auto f = transform_point(c.cameraViewToClip, {0, 0, far, 1});
    close(f[2] / f[3], 1);
    close(c.cameraNear, near);
    close(c.cameraFar, far);
    close(c.mvecScale.x * 1280, 1);
    close(c.mvecScale.y * 720, 1);
    close(c.jitterOffset.x, .25f);
    close(c.jitterOffset.y, -.125f);
    assert(c.cameraMotionIncluded == sl::eTrue && c.motionVectorsJittered == sl::eFalse);
    assert(c.reset == sl::eTrue && c.depthInverted == sl::eFalse);
    const auto clip = transform_point(c.cameraViewToClip, {1, 2, 10, 1});
    auto restored = transform_point(c.clipToCameraView, clip);
    for (int i = 0; i < 4; ++i)
        close(restored[i], std::array<float, 4>{1, 2, 10, 1}[i]);
    restored = transform_point(c.clipToPrevClip, clip);
    for (int i = 0; i < 4; ++i)
        close(restored[i], clip[i]);
    // Translate current camera and rotate previous camera 90 degrees around Y.
    // Current view (1,2,10) -> world (5,4,5) -> previous view (2,5,9).
    camera.inverse_view = {1, 0, 0, 4, 0, 1, 0, 2, 0, 0, 1, -5};
    camera.previous_view = {0, 0, 1, -3, 0, 1, 0, 1, -1, 0, 0, 14};
    camera.previous_projection = {1.5f, 2.f, z, offset};
    c = Vulkan::MakeDlssgConstants(camera, 960, 540, {0, 0}, false);
    const auto previous_clip = transform_point(c.clipToPrevClip, clip);
    const std::array<float, 4> expected{3, 10, z * 9 + offset, 9};
    for (int i = 0; i < 4; ++i)
        close(previous_clip[i], expected[i]);
    // Check the inverse against the independently computed previous clip point.
    // Chaining two float projective transforms amplifies cancellation at near=0.1.
    restored = transform_point(c.prevClipToClip, expected);
    for (int i = 0; i < 4; ++i)
        close(restored[i], clip[i]);
    close(c.cameraPos.x, 4);
    close(c.cameraPos.y, 2);
    close(c.cameraPos.z, -5);
    assert(c.reset == sl::eFalse);
    // 0.4 takes the camera Y convention from each G-buffer pass. A flipped
    // viewport must preserve projection inverses and a positive vendor FOV.
    camera.projection[1] *= -1.f;
    camera.previous_projection[1] *= -1.f;
    c = Vulkan::MakeDlssgConstants(camera, 960, 540, {0, 0}, false);
    assert(c.cameraFOV > 0.f && c.cameraFOV < 3.141593f);
    const auto flipped_clip = transform_point(c.cameraViewToClip, {1, 2, 10, 1});
    restored = transform_point(c.clipToCameraView, flipped_clip);
    for (int i = 0; i < 4; ++i)
        close(restored[i], std::array<float, 4>{1, 2, 10, 1}[i]);
    const auto flipped_previous = transform_point(c.clipToPrevClip, flipped_clip);
    close(flipped_previous[1], -10.f);
    // As above, compare the inverse to an independently projected point:
    // two chained float projective transforms amplify depth cancellation.
    restored = transform_point(c.prevClipToClip, {3, -10, z * 9 + offset, 9});
    for (int i = 0; i < 4; ++i) close(restored[i], flipped_clip[i]);
    std::puts("DLSSG camera: projection/depth, jitter, translation/rotation and "
              "inverse passed");
}
