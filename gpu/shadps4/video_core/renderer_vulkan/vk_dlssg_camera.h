// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>

#include "sl.h"
#include "sl_matrix_helpers.h"

namespace Vulkan {
inline sl::float4x4 DlssgAffine(const std::array<float, 12>& m) {
    // Game matrices operate on column vectors. Streamline uses row vectors.
    sl::float4x4 out;
    for (int r = 0; r < 4; ++r)
        out[r] = {m[r], m[4 + r], m[8 + r], r == 3 ? 1.f : 0.f};
    return out;
}
inline sl::float4x4 DlssgProjection(const std::array<float, 4>& p) {
    sl::float4x4 out;
    out[0] = {p[0], 0, 0, 0};
    out[1] = {0, p[1], 0, 0};
    out[2] = {0, 0, p[2], 1};
    out[3] = {0, 0, p[3], 0};
    return out;
}
inline sl::float4x4 DlssgInverseProjection(const std::array<float, 4>& p) {
    sl::float4x4 out;
    out[0] = {1.f / p[0], 0, 0, 0};
    out[1] = {0, 1.f / p[1], 0, 0};
    out[2] = {0, 0, 0, 1.f / p[3]};
    out[3] = {0, 0, 1, -p[2] / p[3]};
    return out;
}
template <class CameraData>
inline sl::Constants MakeDlssgConstants(const CameraData& c, uint32_t w, uint32_t h,
                                        std::array<float, 2> jitter, bool reset) {
    sl::Constants out;
    out.cameraViewToClip = DlssgProjection(c.projection);
    out.clipToCameraView = DlssgInverseProjection(c.projection);
    auto view_to_world = DlssgAffine(c.inverse_view);
    auto previous_view = DlssgAffine(c.previous_view);
    sl::float4x4 to_previous_view, t;
    sl::matrixMul(to_previous_view, view_to_world, previous_view);
    sl::matrixMul(t, out.clipToCameraView, to_previous_view);
    sl::matrixMul(out.clipToPrevClip, t, DlssgProjection(c.previous_projection));
    // Inverting the complete projective reprojection matrix with a float
    // determinant loses precision at the game's 0.1..3000 depth range.
    // Compose its inverse from the camera's orthonormal view and projection.
    sl::float4x4 to_current_view;
    sl::matrixOrthoNormalInvert(to_current_view, to_previous_view);
    sl::matrixMul(t, DlssgInverseProjection(c.previous_projection), to_current_view);
    sl::matrixMul(out.prevClipToClip, t, out.cameraViewToClip);
    for (uint32_t r = 0; r < 4; ++r)
        out.clipToLensClip[r] = {r == 0 ? 1.f : 0.f, r == 1 ? 1.f : 0.f, r == 2 ? 1.f : 0.f,
                                 r == 3 ? 1.f : 0.f};
    out.jitterOffset = {jitter[0], jitter[1]};
    out.mvecScale = {1.f / w, 1.f / h};
    out.cameraPinholeOffset = {0, 0};
    out.cameraPos = {c.inverse_view[3], c.inverse_view[7], c.inverse_view[11]};
    out.cameraRight = {c.inverse_view[0], c.inverse_view[4], c.inverse_view[8]};
    out.cameraUp = {c.inverse_view[1], c.inverse_view[5], c.inverse_view[9]};
    out.cameraFwd = {c.inverse_view[2], c.inverse_view[6], c.inverse_view[10]};
    out.cameraNear = -c.projection[3] / c.projection[2];
    out.cameraFar = 3000.f;
    // The G-buffer viewport can flip Y. Its sign belongs in the projection
    // and reprojection matrices; the physical field of view stays positive.
    out.cameraFOV = 2.f * std::atan(1.f / std::abs(c.projection[1]));
    out.cameraAspectRatio = float(w) / h;
    out.depthInverted = sl::eFalse;
    out.cameraMotionIncluded = sl::eTrue;
    out.motionVectors3D = sl::eFalse;
    out.reset = reset ? sl::eTrue : sl::eFalse;
    out.orthographicProjection = sl::eFalse;
    out.motionVectorsJittered = sl::eFalse;
    out.motionVectorsDilated = sl::eFalse;
    return out;
}
} // namespace Vulkan
