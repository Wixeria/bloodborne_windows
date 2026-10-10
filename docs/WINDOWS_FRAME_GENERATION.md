# Windows DLSS Frame Generation: experimental game integration

Status on 2026-10-09: **the real game now has an opt-in Streamline Vulkan FG
path. Sustained 2X presentation was measured in a copied Hunter's Dream save
on the RTX 3050 6GB Laptop. The ordinary launch keeps FG off.**

The [October 9 FG review](WINDOWS_FG_REVIEW.md) fixes missing native-1080p
pre-UI capture and a red/blue channel mismatch in HUDless color. Reflex pacing
now starts before the first eligible scene draw and retains that frame's token
through presentation. FG off in the ordinary menu runtime no longer makes the
extra input/color snapshots. Letterboxed windows now receive a window-sized
HUDless image and matching backbuffer/scene rectangle tags. These changes do not
eliminate FG's GPU cost or establish an end-to-end KBM latency improvement.

The [DLSS Super Resolution path](WINDOWS_DLSS.md) remains separate. FG adds
Streamline instance/device/swapchain interception, Reflex/PCL frame tokens,
actual camera constants, immutable depth/motion copies and a tonemapped
HUDless scene captured before the game's UI. The presenter applies the same
host color processing to this scene and tags it separately from the final image.
There is no separate UI alpha recomposition buffer yet; HUD quality in motion
still needs inspection. The Insert FPS counter shows measured rendered and
presented rates when FG is active. Insert now includes **Enable DLSS Frame
Generation** and **FG Multiplier (2X-6X)**. The runtime-reported upper limit
controls which factors are selectable; advertised support is not a measurement
of generated presentations. Short automated camera runs cover 2X and 4X; the user reported
3X-6X working on this host. These factors still need measured latency and image
quality comparisons.
The Insert build smoke (`out/dlssg-menu-smoke-01/summary.json`) passed at 50.5
rendered / 101.0 presented FPS, status 0, with a runtime-reported maximum of 6X.

## Build, deploy and play

Use the pinned SDK already cached by the fixture helper below. Its first
`--capability-only` run downloads it; exit 77 is expected on an RTX 3050 with
the official runtime. The SM86 fork is experimental, not official RTX 30 support.

```powershell
python tools/windows/test_dlssg.py --mode official --capability-only
.\build-windows.ps1 -Gpu -Jobs 3 -Test -StreamlineHeaders out/dlssg-probe-01/sdk/include
python tools/windows/prepare_dlssg_game.py --mode sm86

.\run-windows.ps1 -Gpu -SkipBuild `
  -BuildDirectory out/windows-fg-game -OutputDirectory out/windows-fg-play `
  -UserDirectory out/windows-play-user `
  -OutputResolution 1280x720 -WindowResolution 1280x720 -ResolutionMode startup `
  -Upscaler dlss -UpscalePreset native -PresentMode Immediate `
  -FpsPatch uncap -FpsLimit 120 -SkipIntro -ShowFps -FrameGeneration menu-sm86 -Timeout 0
```

`-FrameGeneration menu-sm86` initializes the experimental runtime and respects
the saved Insert choice. Open Insert, find **Frame Generation (Experimental)**,
check **Enable DLSS Frame Generation**, choose a supported **FG Multiplier**,
and close the menu. Changes apply without a game restart after closing; generation
pauses while Insert is open. The settings save as `frame_generation=0/1` and
`fg_multiplier=2..6` in the selected output directory's `bbport.ini`.

Fresh profiles start with FG unchecked and 2X selected. Start with 2X when testing;
3X-6X are experimental. FG's GPU cost means higher factors do not promise a linear
increase in displayed FPS. The overlay retains a saved factor above a particular
runtime's limit but clamps the applied factor to that supported limit.

`-FrameGeneration dlssg-sm86` explicitly enables FG at launch, overriding a saved
off choice for that session. `-FrameGeneration off` avoids loading the bridge.
For the official runtime use `-FrameGeneration menu` (saved choice) or `dlssg`
(explicitly on), after deployment with `prepare_dlssg_game.py --mode official`
on supported NVIDIA hardware. That official path has not been validated on a
supported RTX card here.
The tested driver requires Immediate presentation; tearing is possible. Keep
the window focused. HDR and swapchains smaller than 720 pixels high are excluded.
The initial hookup takes snapshots from successful DLSS/FSR4 reconstruction;
use the tested DLSS Native AA setting. FSR3/TAA/off do not yet supply FG snapshots.
Fresh gameplay depth/motion is required; loading, reused frames and the open
Insert settings panel disable generation. Unsupported features and initialization
errors are logged and fall back to ordinary presentation where possible.

`-OutputResolution` chooses the scene/upscaler output. `-WindowResolution` also
sets the actual SDL window/swapchain dimensions: a 720p scene in a 1080p window
still gives FG a 1080p output. The experiment copies the SDK and proxy into the
managed `out/windows-fg-game` directory; it does not install the proxy beside the
ordinary executable or change NVIDIA profiles. Runtime signature checks cover
all seven NVIDIA DLLs; the SM86 proxy is checked against its pinned hash.

The default synchronization uses a frame-reuse fence submitted after intercepted
Present, with Streamline's blocking presenting-client-queue mode. It keeps the
snapshots alive through FG consumption without a per-frame queue-idle wait.
`BB_DLSSG_SERIALIZE=1` enables the initial conservative queue-idle path for diagnosis.
`BB_DLSSG_EARLY_SLEEP=0` selects legacy pacing at presentation instead of before
scene draws. `BB_DLSSG_TIMINGS=1` logs CPU stage times, not GPU execution times.
`BB_DLSSG_PARALLEL=1` tests SDK queue parallelism with explicit input-consumption
leases. The provider on this host did not return the required Vulkan timeline;
the experiment safely drains and reverts to blocking mode. It stays off by default.

## Real game measurements

Evidence from the same copied save (720p Native AA scene, uncap patch, RTX 3050 Laptop).
Motion blur and chromatic aberration were disabled in these cases. Fresh manual
launch settings can differ; configure effects in Insert and apply/restart.


| Run | Real FPS | Presented FPS | Result |
|---|---:|---:|---|
| Initial FG, 1080p swapchain, serialized | 43.8 | 87.6 | Sustained 2X; status 0, focused |
| Initial FG, 720p swapchain, serialized | 46.9 | 93.8 | Sustained 2X; status 0, focused |
| Fence path control, 720p, FG off | 67.9 | 67.9 | Status 0, focused |
| Current fence path + HUDless, 720p, FG on | 50.5 | 101.0 | Sustained 2X; status 0, focused |
| Same build + HUDless, 720p, FG off | 70.8 | 70.8 | Matched control; status 0, focused |
| Current FG, camera oscillation, 720p | 50.6 | 101.2 | Sustained 2X in motion; status 0, focused |

Evidence: `summary.json` in `out/dlssg-game-on-02`,
`out/dlssg-game-on-720-01`, `out/dlssg-game-fence-focused-control-01`,
`out/dlssg-game-hudless-on-01`, `out/dlssg-game-hudless-control-01`, and
`out/dlssg-game-hudless-motion-01`. Source save hashes were unchanged.
The current on/control cases use the same executable hash and save snapshot.
These short saved-scene runs are not a complete gameplay benchmark. The initial
FG runs used per-frame serialization; they are not a matched comparison with
the later fence control. An earlier fence-path FG repeat lost focus and was
rejected despite recovering afterward. The current camera test oscillates only
the right stick; it does not cover combat or traversal.

Repeat the integration test with a new case directory for each mode:

```powershell
python tools/windows/test_dlssg_game.py --mode on --output out/fg-on-new --seconds 30
python tools/windows/test_dlssg_game.py --mode control --output out/fg-control-new --seconds 30
# Optional camera oscillation in the copied save:
python tools/windows/test_dlssg_game.py --mode on --camera-motion --output out/fg-motion-new --seconds 20
```

The helper copies `out/windows-play-user` saves/cache, opens the saved scene with
its existing file-based pad harness, warms up, captures an original presentation
and the pre-UI scene, then records guest flips and Streamline presentation counts.
It requires status 0, uninterrupted focus and a steady presentation ratio near
2X (on) or 1X (control). Logs, `frames.csv`, captures and `summary.json` stay in the
case directory. Capturing an original frame does not prove generated pixel quality.

The matched current tests increase steady presentation cadence by about 43%
(70.8 to 101.0 FPS), while rendering drops from 70.8 to 50.5 FPS with FG overhead.
Guest flip measurements over the full 30-second phases were 70.9 FPS (off) and
49.0 FPS (on), with p99 flip intervals 24.4 ms and 33.4 ms respectively. The
captures confirm the pre-UI image contains the scene without the game's HUD;
they do not capture NVIDIA's generated frames.

Generated presentations increase display cadence, not game simulation speed.
FG adds GPU work and does not turn 50 rendered FPS into 100 real gameplay FPS.
Reflex/PCL calls currently track renderer arrival and presentation, not complete
guest input/simulation boundaries; no end-to-end latency benefit is claimed.

## Components and provenance

- NVIDIA [Streamline v2.14.1](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1),
  Windows x64 release ZIP SHA256
  `92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b`.
- Experimental RTX 30 Vulkan bridge from
  [SilyNoMeta/dlssg_for_sm86](https://github.com/SilyNoMeta/dlssg_for_sm86),
  commit `329b4c85927c64a8dba2e25125e6d02a42ab63fe`, based on the
  [sdli1995 project](https://github.com/sdli1995/dlssg_for_sm86).
  Proxy SHA256
  `7489a89bf593cda243d95c62e6d8b75905d9774e27b7b2d53b91625a79c05ca4`.
  The original project's current release targets D3D12; this fork supplies
  Vulkan-specific configuration and requires a matching NVIDIA runtime.
- NVIDIA `nvngx_dlssg.dll` 310.9.1, SHA256
  `ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82`.
  The SDK already supplies this exact file; no extraction/replacement of game
  DLLs is needed.

The helper verifies download hashes and the Authenticode signatures of the
seven NVIDIA DLLs before executing the fixture. The experimental proxy is
checked against its pinned SHA256. DLLs, their notices, logs, and executables
stay under ignored `out/` directories. NVIDIA's libraries remain subject to
their own terms; this test does not add them to the repository.

SDK preferences disable optional OTA downloads/loading, but the tested
machine has an NVIDIA App driver override. Streamline still loads driver-managed
2.14.0 plugin overrides and observes another DLSS-G provider, 310.2.1. The
deployed package and actual loaded plugins are therefore distinct: consult
`logs/sl.log` and `DLSSG-SM86-75-runtime.log`, not just the ZIP version.
The helper does not change NVIDIA App profiles, global driver settings or caches.

## Run the fixture

Existing LLVM-MinGW, SDL3 and Vulkan-Headers from `build-windows.ps1` are used.
Run from the repository root:

```powershell
# Expected exit 77 on RTX 3050: official NVIDIA FG is unavailable.
python tools/windows/test_dlssg.py --mode official --capability-only

# Experimental RTX 30 bridge: visible moving rectangle over a checkerboard.
python tools/windows/test_dlssg.py --mode sm86

# Same bridge/resources/presentation, interpolation explicitly disabled.
python tools/windows/test_dlssg.py --mode sm86-off
```

The helper builds only `tests/dlssg_vulkan_probe.cpp`; it does not rebuild or
launch `bb-game`. Downloads are cached in `out/dlssg-probe-01`. Default run
directories are `out/windows-dlssg-probe/{official,sm86,sm86-off}`.
`--output`, `--cache`, `--toolchain`, `--sdl`, and `--vulkan-headers` can override
those locations; executable output must stay under this workspace's `out/`.
The helper refuses to overwrite an unrelated existing run directory.

Keep the probe window focused until it closes. The initial focus wait is 15
seconds; the process deadline is 60 seconds. Exit codes:

| Exit | Meaning |
|---|---|
| 0 | Requested capability/presentation checks passed |
| 1 | API error, invalid measurement, or insufficient generated presentations |
| 77 | FG unavailable on the selected NVIDIA device |
| 124 | Process exceeded the deadline and was terminated |

Results are in `probe.log`, `build.log`, `summary.json`, and `logs/sl.log`.
The mode directory is reused on subsequent runs; copy its logs first if you
need to retain a previous run. An unsupported official capability check is an
expected result, not a successful frame-generation test.

## What is measured

The fixture uses a real Win32 Vulkan surface and swapchain, Streamline instance
and device proxies, an explicit NVIDIA adapter, full-size scene color, depth
and dense 2D motion inputs. A rectangle moves four pixels per real frame;
its vectors point to the preceding position, while the background is static.
The synthetic scene has no HUD and uses no DLSS Super Resolution.

Reflex sleep and PCL simulation/render/present markers share each frame token.
The pipeline requests one generated frame per real frame. Immediate presentation
is required by this tested driver because it reports no FG VSync support.
The fixture deliberately serializes GPU work and adds a 20 ms delay; it is
**not a performance benchmark**. Default `eBlockPresentingClientQueue` and a
queue idle wait protect reused inputs. Calling Streamline's device-idle hook
each frame flushes the presentation worker and prevented sustained generation
in the early fixture; device idle is reserved for teardown.

`DLSSGState::numFramesActuallyPresented` counts presentations, not merely
requested multipliers. After 30 warm-up frames, the enabled test requires at
least 114 presentations for 60 submitted frames (95% of the expected 120);
the disabled control requires exactly 60. Nonzero FG status fails the test.
The mod can report support on Intel as well: the fixture deliberately rejects
those adapters and selects the physical NVIDIA device by its real vendor ID.

This verifies sustained presentation counts and error-free API status in a
synthetic workload. It does not validate generated pixels against a reference,
Bloodborne HUD quality, input latency, long-session stability or gameplay FPS.

## Recorded results

Hardware: NVIDIA GeForce RTX 3050 6GB Laptop, driver 617.14, HAGS already enabled.

| Run | Submitted | Presented | Last 60 submitted | Status / result |
|---|---:|---:|---:|---|
| Official capability | — | — | — | Unsupported, exit 77 |
| Experimental 2X, paced Immediate | 90 | 180 | 120 presentations | Status 0, exit 0 |
| Experimental 2X, focused repeat with helper | 90 | 180 | 120 presentations | Status 0, exit 0 |
| Experimental bridge, FG off | 90 | 90 | 60 presentations | Status 0, exit 0 |

Initial passing evidence:
`out/dlssg-probe-01/sm86/on-paced.log` and `on-paced-sl.log`.
The disabled control also passed through the checked-in helper:
`out/windows-dlssg-probe/sm86-off/summary.json`.
The focused repeat passed with the explicit per-frame focus check:
`out/windows-dlssg-focused-01/sm86/summary.json` and `probe.log`.

Subsequent repeats with the explicit per-frame focus check stopped when the
window lost focus. NVIDIA's log confirms `DLSS-G disabled: window not focused`;
the test rejects this instead of counting it as a pass. One repeat also hung
after loading the experimental proxy and was terminated by the 60-second
deadline. After the user kept the test window focused, the checked-in helper
completed all 90 frames with 180 presentations. Thus the focus failure is
explained, but the occasional initialization timeout remains unresolved; the
successful measurements are **not** a claim of reliable launches or long sessions.

## Remaining validation and integration

- Inspect moving character/camera, transparencies and HUD on generated frames;
  add a separate UI color/alpha buffer before enabling UI recomposition.
- [Guest pad-to-submit Reflex markers and independent modes](WINDOWS_REFLEX.md)
  are implemented. Verify the entire CPU simulation boundary and measure
  input latency separately; the current runtime reports a zero input timestamp.
- Validate restart, resolution changes and longer sessions. The present smoke
  helper exits through the existing stop-file watchdog, so it does not exercise
  every graceful shutdown path.
- Validate the official path on supported hardware and measure the user-tested
  3X-6X factors. Insert and the [Windows launcher](WINDOWS_LAUNCHER.md) both expose
  the same persistent FG controls.
- Investigate occasional proxy initialization hangs seen in the fixture.

References: NVIDIA's [FG guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md),
[Vulkan hooking guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md),
and [Reflex guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideReflex.md).
