# Windows DLSS Super Resolution

DLSS is integrated into the Vulkan scene-color/depth/motion-vector/jitter path.
This is **Super Resolution**, without frame generation. The default remains off:
Quality mode did not improve FPS on the tested RTX 3050 6GB Laptop GPU.

The separate [experimental Frame Generation integration](WINDOWS_FRAME_GENERATION.md)
now supplies an opt-in Streamline/SM86 path in `bb-game`. It needs its own build,
private deployment and launch flag; `-Upscaler dlss` alone enables only SR.

## Run

Build the GPU executable, then fetch the pinned NVIDIA model:

```powershell
.\build-windows.ps1 -Gpu -Jobs 2 -Test
python tools/windows/fetch_dlss.py
.\run-windows.ps1 -Gpu -SkipBuild -Upscaler dlss -UpscalePreset quality -ShowFps -Timeout 0
```

The fetcher verifies `nvngx_dlss.dll` against SHA-256
`3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983`.
The model and SDK license stay in `out/windows`, outside source control.
Source: [NVIDIA DLSS v310.9.1](https://github.com/NVIDIA/DLSS/tree/v310.9.1).
An existing matching model can be reused with `--source <path>`.
For a separate build use `--destination out/windows-lto`; the runner also accepts
`-DlssDirectory <directory>`. Direct executable launches use `BB_DLSS_DIR`,
`BB_UPSCALER=dlss`, `BB_UPSCALE_PRESET=quality` and optionally `BB_SHOW_FPS=1`.

The Insert overlay offers DLSS only when the device, driver NGX and model are
available. Unsupported startup requests fall back to FSR 3. Creation/evaluation
failures also switch to FSR 3. Check `game.log` for the actual provider and
`DLSS: 1280x720 -> 1920x1080, quality mode 2`; the selected menu option alone is insufficient evidence.
Use `-Upscaler off` to return to native rendering.

## Validation and lifetime

The native build passes 87/87 CTests, including six real settings/capability
cases. Manual `dlss-vulkan-test.exe` runs the production backend on the actual
GPU: HDR color, depth and motion inputs, poisoned output replaced by finite,
nonzero RGB, resize/recreation, and two NGX initialization/shutdown cycles.
`texture-cache-vulkan-test.exe` still passes. These GPU tests are separate from
CTest because they require hardware and the model. Evidence:
`out/dlss-integration-review/gpu-validation.json` and
`out/dlss-integration-review/native-build-final.log`.

Features are released after scheduler completion and before their image views;
provider changes and resize invalidate the old feature. NGX search-path/data
storage remains alive for the context. Windows NGX core is loaded once per
process. The tested driver's raw `Shutdown1` export requires a second writable
32-bit output pointer, unlike the public SDK wrapper signature. The adapted
Windows ABI was verified by driver disassembly and clean GPU/game exits; Linux
retains the upstream signature and has not been tested here. This raw-driver
interface may need verification on other driver versions.

The GPU integration tests use the same Vulkan-Hpp release/platform layout as
`bbgpu` while keeping their test assertions enabled.

## Gameplay result

`out/perf-dlss-quality-02` loads the outdoor save and completes three 30-second
camera/R1 passes. Output is 1920x1080, input 1280x720, DLSS Quality, object motion
on, internal vblank 480 Hz, frame cap 60, automatic helpers. Detailed diagnostics,
FPS overlay, GPU timestamps and thread CPU sampling are off. Saves and pipeline
cache are independent copies; both sources remain unchanged. Process exit is 0.

| Pass | FPS | p99 ms | Shader compiles | Frames >50 ms |
|---|---:|---:|---:|---:|
| 1 | 44.09 | 42.89 | 15 | 7 |
| 2 | 49.05 | 33.82 | 0 | 3 |
| 3 | 50.29 | 29.70 | 0 | 3 |

The earlier native register-scan run measured 54.86-55.73 FPS. These sequential
runs do not establish a controlled provider-only speed difference, but they
provide no evidence of a DLSS gain. The pinned model's Quality default is
transformer preset K; NVIDIA documents its higher performance cost in
[the pinned SDK definitions](https://github.com/NVIDIA/DLSS/blob/v310.9.1/include/nvsdk_ngx_defs.h).
Other presets/models need their own measurements. Existing foliage/geometry
artifacts remain; this short route does not validate combat, transitions or
long-session image quality. Locked 60 FPS is not achieved.

## Separate performance experiments

ThinLTO is opt-in and covers the root runtime as well as the renderer:

```powershell
.\build-windows.ps1 -Gpu -Lto -BuildDirectory out/windows-lto -Jobs 2 -Test
```

Default builds remain non-LTO. An unsupported explicit LTO request fails at
configuration rather than silently producing a non-LTO binary. The separate
LTO build also passes 87/87 CTests and both actual GPU tests.
A sequential native-rendering pair measures 55.68 FPS non-LTO versus 53.96 FPS
LTO, providing no gain; it stays opt-in. Details and limits are recorded in
[Windows performance measurements](WINDOWS_PERFORMANCE.md#thinlto-experiment).

`tools/windows/profile_game.py --cpu-threads` optionally writes `thread-cpu.csv`
and summarizes execution time per Windows thread. It does not suspend threads,
collect call stacks or measure waits. Sampling adds diagnostic overhead; keep
these runs separate from FPS comparisons. No scheduler/fence removal has been
made on the basis of this accounting tool alone.


The first actual outdoor accounting run is `out/perf-thread-cpu-01`. Largest
named CPU users are GpuCommandProcessor, DrawRec and VkRecorder. Results and
interpretation limits are in [the CPU accounting section](WINDOWS_PERFORMANCE.md#opt-in-thread-cpu-accounting).
