# Frame generation review — 2026-10-09

HUDless input defects were fixed. FG's render-rate overhead remains;
generated frames and end-to-end mouse latency are not measured by these captures.

## Changes

- Native 1080p output now snapshots the first recognized Scaleform movie/stencil
  pass, including shader `0x81d336ce`. Waiting only for the text shader omitted
  HUDless color in the user's 1080p / DLSS Quality / live-resolution profile.
  Each snapshot is captured once, before any HUD draw.
- HUDless color now uses the same RGBA/BGRA interpretation as the final display
  buffer. The old scene view left red and blue reversed. No extra GPU color copy
  is needed: a swizzled sampled view supplies the right interpretation.
- Reflex sleep runs before the first eligible scene draw, with its frame token
  attached to the immutable input snapshot through presentation. Legacy late
  pacing remains a diagnostic fallback. These are renderer boundaries, not
  instrumented guest simulation/input boundaries.
- The normal saved-menu runtime skips depth/motion/HUDless copies when FG is off.
  Explicit `control` experiments retain them for a matched comparison.
- Insert shows the actual FG output dimensions. Borderless uses desktop dimensions;
  reducing DLSS render size alone does not lower the FG output size.
- Optional CPU stage timings separate begin/pacing, image acquisition, submission
  mutex and present. The test helper supports native/scaled output, live resolution,
  window mode, multipliers, warmup and a required-HUDless check.
- `BB_DUMP_FG_COLOR_TRIGGER` captures actual host-processed final/HUDless images
  from the same guest frame. Captures now run on the presentation scheduler after
  the final window blit and overlay. `check_dlssg_colors.py` checks their scene-region colors.

### Window dimensions and letterboxing

A fixed 720p scene in a 1080p 16:9 window was already scaled into window-sized
presentation frames. The initial suspicion that this profile necessarily omitted
HUDless color was incorrect; the same-frame 1080p capture confirms matching colors.

The separate case is a window with a different aspect ratio, or a frame prepared
before a window resize. `SetExpectedGameSize` fits a presentation frame inside the
window; its HUDless target can therefore be smaller than the swapchain. Previously
`Tag` omitted that target because its dimensions did not match FG's output.

`PresentHudless` now applies the backbuffer's exact black clear, linear blit and
letterboxing to a separately leased window-sized image when the dimensions differ.
Matching sizes reuse the original image, with no extra blit or allocation. Input
reuse waits cover this image through the existing presentation/plugin fences.

The backbuffer and HUDless tags also carry the same scene rectangle, as required
by the [Streamline tagging guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md#52-tagging-recommendations).
Depth/motion retain their scene dimensions, while black bars remain outside the
generated region. A full-window rectangle resets the crop for a 16:9 window.
These input and tagging checks do not establish generated-pixel quality.

The final window-fix executable is SHA256
`015c829150d565633d1c21a110e7435a8bb385a202460ad968916b8da1b58528`.
It passed two 10-second camera-motion checks after six seconds of warmup:

| Case | Tagged scene region (left, top, size) | Real / presented FPS | RGB mean / max error |
|---|---|---:|---:|
| 720p Native AA in 1280x960 window | 0, 120, 1280x720 | 46.37 / 92.74 | 0 / 0 |
| 1080p Quality, borderless | 0, 0, 1920x1080 | 39.74 / 79.50 | 0 / 0 |

Both had SDK status 0, continuous focus, actual presentation ratio approximately
2, normal exit and unchanged source saves. The 4:3 capture samples 181,804 pixels;
the 1080p capture samples 307,020. Final/HUDless images were also inspected: the
clean image omits HUD text/bars, and both have the same centered black borders.
The 1080p run takes the matching-size path without the additional blit.

These are short input/crop regression checks with different warmup and profiles,
not a new matched FPS or input-latency comparison. The earlier matched control/on
pair below remains the evidence for rendered-FPS overhead. Continuous window
resizing and generated-frame recomposition still need broader coverage.

Evidence: `out/fg-review-20261009-letterbox-region-fixed` and
`out/fg-review-20261009-full-region`. One initial run stopped before gameplay with
a `Wrong PM4 type 1` assertion (exit 23); its repeat passed. An intermediate run
was rejected for transposed rectangle left/top coordinates despite SDK status 0.
The helper now checks actual rectangle position and preserves the original failure
reason when the game exits before producing HUDless input.

The tested executable was deployed to the launcher's `out/windows-fg-game` runtime.
The prior executable/manifest remain in `out/fg-review-20261009-window-deployment`.
Pinned DLL hashes, launcher preferences and the live game's INI were unchanged.
The user reported that the earlier fixes felt better; this is subjective feedback,
not an end-to-end mouse latency measurement.

## Measurements

RTX 3050 6GB Laptop, copied existing save, Immediate, uncapped guest FPS,
motion blur/chromatic aberration off. 1080p cases use DLSS Quality with live scene
rendering at 1280x720. The true 720p window uses DLSS Native AA at 1280x720.

| Case | Real FPS | Presented FPS | Guest flip p99 |
|---|---:|---:|---:|
| Initial 1080p control, static | 53.94 | 53.94 | 31.15 ms |
| Initial 1080p 2X, static, missing HUDless | 39.10 | 78.19 | 37.64 ms |
| Correct color, legacy late pacing, static | 39.85 | 79.70 | 37.68 ms |
| Preliminary early pacing, static | 40.80 | 81.60 | 27.80 ms |
| True 720p 2X, preliminary early pacing, camera motion | 46.70 | 93.39 | 23.72 ms |
| Final 1080p control, camera motion | 57.07 | 57.07 | 20.90 ms |
| Final 1080p 2X, camera motion | 41.10 | 82.21 | 41.54 ms |
| Final 1080p 4X, camera motion (10 seconds) | 31.37 | 125.47 | 45.68 ms |

The final control/on pair uses the same executable SHA256:
`f8391afdb2fb3c2e42f0cba00324c109a3ce3b6a4e7df4d39e3ce8aeec2bbc5c`.
Both passed status 0, uninterrupted focus, actual presentation-ratio checks,
normal exit and unchanged source-save hashes. Samples are 16 seconds after warmup;
the 720p and preliminary pacing cases are separate profiles/iterations, not a
matched claim of FPS or latency gain over the final pair.

The same binary also passed a short 4X camera run (measured presentation ratio
4.0002). Its lower real-frame rate illustrates why a larger presentation number
does not mean better mouse response. Longer 4X sessions, 3X/5X/6X, combat and
generated-pixel quality remain outside these automated checks.

The final pair retains approximately **28% rendered-FPS overhead** with 2X FG.
Presented cadence rises about 44%. Early pacing moves roughly 20 ms of waiting
out of the late presentation stage, but it does not remove that GPU workload.
The control rate also changed between iterations, so the small 39-to-41 difference
must not be attributed entirely to the code. Moving-camera tail times do not
demonstrate a broad stutter improvement. No end-to-end KBM latency claim is made.

### Color checks

The actual host-processed inputs match exactly in the sampled scene region:
307,020 pixels at 1080p and 136,654 at 720p, RGB mean/max error **0 / 0**.
The region excludes the HUD across the top. Intentionally comparing red/blue
reversed produces roughly 8.3 / 8.2 levels of mean 8-bit error. The clean images
were also visually inspected for missing HUD content. These are original inputs,
not NVIDIA's interpolated output, combat/traversal coverage or a genuine UI alpha plane.

### Rejected performance experiments

The provider did not expose `inputsProcessingCompletionFence` and its value even
after a bounded startup interval. The optional parallel queue path therefore
reverted to the conservative blocking path; no parallel-queue speedup is claimed.
Outstanding input leases are awaited before reuse, option changes and drain.

Skipping Reflex sleep reduced the test to about **16.3 real / 32.6 presented FPS**.
That diagnostic is not a recommended configuration and is not enabled by default.
The default retains Reflex and moves it earlier instead of deleting pacing.

## Reproduce

```powershell
python tools/windows/test_dlssg_game.py --mode on --output out/fg-repeat-on `
  --output-res 1920x1080 --window-res 1920x1080 --window-mode borderless `
  --preset 1 --live-resolution 1 --fps-limit 0 --seconds 30 `
  --camera-motion --require-hudless --timings
python tools/windows/check_dlssg_colors.py out/fg-repeat-on/capture
```

Repeat with `--mode control` and a fresh output directory for the comparison.
Use `--no-early-sleep` for legacy presentation pacing and `--parallel` only for
the explicit queue experiment. Settings, logs, original captures, measured flips,
executable hashes and summaries remain under each case in `out/`.

To exercise window-sized HUDless output and the letterbox rectangle:

```powershell
python tools/windows/test_dlssg_game.py --mode on --output out/fg-repeat-letterbox `
  --output-res 1280x720 --window-res 1280x960 --window-mode windowed `
  --preset 0 --live-resolution 1 --fps-limit 0 --seconds 10 `
  --camera-motion --require-hudless --timings
python tools/windows/check_dlssg_colors.py out/fg-repeat-letterbox/capture
```

Evidence directories use prefix `out/fg-review-20261009-`: `baseline-off-02`,
`baseline-on`, `native-hudless-on`, `color-fixed-1080`, `early-sleep`,
`early-720-motion`, `no-late-sleep`, `final-control`, `final-on` and `final-4x`.

Five selected host checks passed: camera constants, virtual pad, window modes,
DLSS settings available/unavailable. The SDK-disabled C++ branch also passed
syntax compilation. Pinned DLLs and NVIDIA global profiles/caches were not changed.

## Remaining work

Measure generated pixels during camera movement and combat, then supply actual
UI alpha coverage for recomposition. Measure guest input/simulation boundaries
before claiming Reflex/KBM latency gains. Profile actual GPU FG work on this SM86
provider; the current test does not establish its GPU kernel time or a way to
eliminate the remaining render-rate overhead.
