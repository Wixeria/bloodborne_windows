# Windows performance measurements — 2026-10-05

The Windows port now records guest flip intervals, process RAM, Vulkan heap
usage/budget, shader compilations and queued image/buffer upload ranges. The
profiler runs a scripted camera route against an independent save copy. Physical
keyboard/controller input is excluded during profiling, so it cannot change the
route or start a different game accidentally.

## Memory correction

The legacy texture-cache policy reported pressure at 1,831 MiB while the driver
allowed 5,233 MiB and the clinic used about 2.4 GiB. Windows now derives its GC
trigger, pressure and critical thresholds from the **current** Vulkan budget:
70%, 85% and 95%. The measured pressure threshold is about 4,448 MiB. The
budget is queried together with usage, so changing driver allowances are used
on the next GC pass. A missing/zero budget retains fallback thresholds.

Driver heap usage is also kept separate from logical guest-image sizes. Removing
a guest image no longer subtracts its guest byte count from the driver's usage;
GPU allocations can have different sizes and be released asynchronously.
`BB_GC_CURRENT_BUDGET=0` selects the legacy threshold policy for diagnostics;
`BB_GC_BUDGET_MB` remains an explicit test override. Linux's default threshold
policy is retained; Linux regression execution was unavailable.

This corrects premature pressure decisions. The short clinic tests do **not**
establish a sustained FPS gain, sufficient behavior near exhaustion, or stability
across large area transitions.

## Initial measurements

Hardware: NVIDIA GeForce RTX 3050 6GB Laptop GPU. Game: supplied 01.09 update,
1920×1080, upscaling off, existing Lance McDonald 60-FPS patch in the source
executable. Save: `w1x`, level 30, loaded inside Iosefka's Clinic. No enemies or
walking are included: the 20-second route alternates idle and camera rotation,
repeated three times in each process.

| Run / pass | Mean guest flips/s | p99 interval (ms) | New compilations | Frames >50 ms | Peak resident RAM (GiB) | Peak local GPU heap (GiB) |
|---|---:|---:|---:|---:|---:|---:|
| Legacy / 1 | 50.20 | 34.70 | 80 | 5 | 4.084 | 2.606 |
| Legacy / 2 | 52.31 | 34.12 | 2 | 0 | 4.083 | 2.606 |
| Legacy / 3 | 53.62 | 33.99 | 0 | 0 | 3.976 | 2.544 |
| Current budget / 1 | 50.37 | 34.50 | 79 | 4 | 4.117 | 2.636 |
| Current budget / 2 | 50.48 | 35.19 | 2 | 4 | 4.114 | 2.574 |
| Current budget / 3 | 51.86 | 34.58 | 0 | 2 | 3.979 | 2.574 |
| Final counters / 1 | 51.27 | 34.37 | 79 | 5 | 4.057 | 2.661 |
| Final counters / 2 | 52.26 | 34.16 | 2 | 0 | 4.054 | 2.598 |
| Final counters / 3 | 54.41 | 33.92 | 0 | 0 | 3.987 | 2.598 |

These are separate observations, not proof of a performance improvement.
Shader caches start empty for each process, but title/menu/loading shaders
compile before measurement; pass 1 is the first measured camera route, **not**
total cold-start performance. Later passes reuse the same process's cache.
The compilation count falls to zero, but that alone does not prove the cause
of every hitch. Thermal state, frame-dependent camera motion and background
load can affect comparisons. No memory leak conclusion follows from 60 seconds.

Raw evidence:

- `out/perf-baseline-03/`: legacy thresholds and old memory accounting.
- `out/perf-current-01/`: current budget thresholds and corrected accounting.
- `out/perf-final-01/`: three passes with final upload counters and batched CSV.
- `out/perf-final-02/`: one additional pass verifying automatic summary and
  successful diagnostic exit (status 0, 75.8-second driver teardown wait).
- Each directory includes `game.log`, `frames.csv`, `ram.csv`, `phases.json`,
  `summary.json`, `session.json` and SHA-256 source-save manifests.

All 18 source save files remained unchanged. The initial runs completed their
routes but exceeded the process-termination wait. Their summaries were
regenerated from complete raw measurements. The GPU driver can finish teardown
after the diagnostic stop has already been logged; the profiler now allows up
to 90 seconds for that completion and writes `termination.json` on success.

The corrected counters still show 9.24–9.61 GiB of queued image ranges and
2.82–3.07 GiB of buffer uploads per 20-second final pass. In this route the mip
hash skip did not reduce the image counter. That is repeated processing of
resources, not 9 GiB of simultaneously occupied VRAM. The command-thread logs
also show roughly 1,000 draws per flip with very little idle time. These identify
upload work and command processing as useful candidates for deeper timing;
they do not establish which one limits the displayed frame rate.

## Outdoor follow-up

`out/perf-outdoor-01/` uses an independent copy of the earlier imported save
under `out/windows-shad-save-20261005-020108/user/`. That copy's character
position is outdoors, unlike the original shadPS4 save's clinic position.
Walking and R1 animation were checked before timing; the measured route then
repeats camera rotation and three R1 inputs for six 30-second passes.

| Pass | Mean guest flips/s | p99 interval (ms) | New compilations | Frames >50 ms | Peak resident RAM (GiB) | Peak local GPU heap (GiB) |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 49.13 | 34.43 | 49 | 4 | 4.279 | 3.022 |
| 2 | 50.37 | 34.74 | 0 | 3 | 4.281 | 3.022 |
| 3 | 51.50 | 34.21 | 0 | 0 | 4.282 | 3.022 |
| 4 | 51.07 | 34.26 | 0 | 0 | 4.283 | 3.022 |
| 5 | 46.98 | 34.55 | 0 | 5 | 4.284 | 3.022 |
| 6 | 46.39 | 34.47 | 0 | 0 | 4.283 | 3.022 |

The complete three-minute route finishes without a fatal/unsupported-import
stop, then exits with diagnostic status 0. The source copy is unchanged.
Resident RAM and local GPU heap peaks stay approximately flat in this sample;
this does not establish leak-free behavior over long sessions. No enemy damage,
death or area transition is exercised by this standing attack route.

`out/perf-outdoor-gpu-01/` repeats two 30-second passes with `--gpu-timing`.
It also exits 0 and preserves its source. Representative later timestamp
reports show 19.5–23.1 ms/frame total intervals, with 5.1–8.5 ms/frame labelled
"between submissions: GPU idle". Together with the busy command-recording
thread and frequent ConstRam drains, this points to CPU preparation/submission
and synchronization as the next profiling target. It does not prove that all
gaps can be removed: guest pacing and the instrumentation can contribute.
These timing windows include barriers and gaps; FPS from this run is not an
equivalent comparison against the ordinary route.

## Forced-pressure diagnostic

Two additional runs use `--gc-budget-mb 2800` and the same 30-second outdoor
camera/R1 route. This lowers the pressure/critical thresholds to 2,380/2,660 MiB
while the driver's allowance remains 5,233 MiB. It exercises the pressure branch
without reserving extra VRAM or simulating physical exhaustion.

| Run / pass | Mean guest flips/s | p99 interval (ms) | Maximum interval (ms) | New compilations | Frames >100 ms |
|---|---:|---:|---:|---:|---:|
| Pressure / 1 | 46.81 | 49.26 | 66.29 | 49 | 0 |
| Pressure / 2 | 45.49 | 34.44 | 800.46 | 0 | 3 |
| Pressure with timers / 1 | 45.75 | 34.67 | 50.67 | 49 | 0 |
| Pressure with timers / 2 | 46.61 | 34.35 | 35.23 | 0 | 0 |

The first run (`out/perf-pressure-01/`) contains an 800 ms warm hitch with no
new compilation. The command-thread report attributes roughly 734 ms to CPU
execution, including 500 ms kernel time, but the existing sub-operation counters
do not explain it. That is an unresolved observation, not evidence of VRAM
exhaustion or a GC stall. Thread CPU counters have coarse Windows resolution.

`out/perf-pressure-timing-01/` adds separate driver-budget-query and GC-scan
timers. Its automatic summary excludes the first memory-report window in each
phase, since that window can include time before measurement starts:

| Pass | Driver query mean / max (ms) | GC scan mean / max (ms) |
|---|---:|---:|
| 1 | 0.0120 / 0.3965 | 0.0132 / 0.4587 |
| 2 | 0.0121 / 0.3624 | 0.0140 / 0.3279 |

The 800 ms hitch did not recur in this repeat. Budget queries and GC scans are
cheap in the measured repeat, so there is no measured justification for reducing
query frequency or weakening synchronization. These timings cannot determine
the cause of the earlier hitch. Both pressure runs report zero image evictions
and downloads; this validates neither reclamation of inactive assets nor recovery
from actual exhaustion. The active scene continues to render, and source-save
verification remains unchanged. Both runs exit with diagnostic status 0; the
timer run takes 85.2 seconds to finish Windows driver teardown. All 74 native
CTests passed after the timing changes. The subsequently extended Python
summary test also passes through CTest. Original shadPS4 save files, the imported
source-save copy and both original base/update executables retain their SHA-256
hashes.

## Reproduce

Prepare the 01.09 game first with `run-windows.ps1`, then build the renderer:

```powershell
.\build-windows.ps1 -Gpu -Jobs 2 -Test
python .\tools\windows\profile_game.py --source-save "$env:APPDATA\shadPS4\home\1000\savedata\CUSA00207\SPRJ0005" --output 'out\perf-new' --ready-timeout 900
```

The output directory must be new. Defaults use `out/windows/bb-game.exe` and
the prepared game under `out/windows-game-109`; override `--exe` or `--prepared`
if needed. The source save is copied as ordinary files, never linked or mounted.

The profiler intentionally waits for an inspected gameplay scene. While it is
waiting, use a second PowerShell terminal to inject menu input:

```powershell
# Confirm the highlighted item (Play Offline, then Continue on this save).
Set-Content -LiteralPath .\out\perf-new\pad.txt -Value 'cross' -Encoding ascii
Start-Sleep -Milliseconds 300
Set-Content -LiteralPath .\out\perf-new\pad.txt -Value '' -Encoding ascii
# Repeat only after the next menu is visible. Once the character/HUD is ready:
Set-Content -LiteralPath .\out\perf-new\ready.trigger -Value 'ready' -Encoding ascii
```

The route then runs without host input. `--route` accepts a JSON list of
`{"seconds": 3, "pad": "rx=255"}` steps; see `tools/windows/camera-route.json`.
Do not use `ready.trigger` during a loading screen. At completion the profiler
requests a diagnostic process stop through `BB_STOP_FILE`, with no driver DLL
detach or normal in-game save/quit. Any save writes belong to the independent
test copy. Ordinary interactive runs do not enable this stop file.
Allow the profiler to finish the driver teardown wait before launching another
GPU test. A failure retains raw measurements and source-save verification.

With `BB_MEMORY_STATS=1`, newer logs additionally record cumulative driver-query
and GC-scan counts/times (`query_count`, `query_ns`, `scan_count`, `scan_ns`).
`query_max_ns` and `scan_max_ns` are maxima since the previous memory report.
Scan time includes its mutex wait and eligible image cleanup; driver-query time
is measured separately. `summary.json` includes `gc_timings` over the interval
between in-phase reports; it is null for older logs or insufficient samples.
These optional timings leave ordinary play unchanged.

Recalculate a summary without launching a game:

```powershell
python .\tools\windows\profile_game.py --summarize .\out\perf-new
```

## Counter meanings

- Frame CSV uses the same Windows monotonic clock as the Python sampler. It
  measures guest flips, not exact monitor presentation. Start/menu/loading
  frames and intervals crossing route boundaries are excluded from summaries.
- RAM working set is resident process memory. Private commit excludes shared
  guest mappings. These are separate counters, not values to add together.
  RAM is sampled every 500 ms and GPU memory approximately once per second;
  reported peaks are sampled maxima, not continuous maxima.
- On a discrete GPU, local heap usage includes images, buffers and driver
  allocations. Host GPU heap usage is a separate Vulkan figure, not total RAM.
  Registered guest images are logical image ranges, not VRAM allocation sizes.
- Image uploads count guest mip ranges actually queued for image copies,
  including layout padding; they do not measure PCIe bus traffic. Refresh-range
  bytes include checks skipped when mip contents are unchanged. The initial CSV
  schema mislabeled refresh-range bytes as uploads; the summarizer recognizes
  it and reports actual uploads as unavailable for those runs.
- Buffer uploads are the renderer's existing upload counter. GPU eviction and
  download fields in `game.log` are counters since the previous pressure report,
  not cumulative totals. Do not sum their one-second snapshots.
- Frame rows are batched before writing once per second, keeping per-frame
  file flushes out of this diagnostic path. Profiling still has some overhead.

`--gpu-timing` enables the existing `BB_GPU_PROFILE` Vulkan timestamp tool.
It prints render-pass/transfer/dispatch labels and their intervals to `game.log`.
This diagnostic changes command recording and can add overhead; use it to locate
expensive intervals, not to compare its FPS directly with ordinary profiling.
An interval can include barriers, queue gaps and CPU starvation between recorded
commands, so it is not a pure hardware utilization measurement.

`--gc-budget-mb 2800` overrides only the GC threshold budget (in MiB). It does
not reserve VRAM or alter the driver-reported allowance. Use it on an isolated
save copy to exercise pressure behavior; it is not a normal performance preset.
`tools/windows/camera-attack-route.json` is a 30-second camera/R1 route for a
standing character; attack animations alone do not validate enemy combat.

The next profiling target is CPU command preparation/submission and its
synchronization, including a repeat that captures the rare warm hitch. Gameplay
validation still needs enemy combat, death/reload and an area transition. The
current standing attack route does not cover those behaviors.

## PR 6 adaptation

Four corrections from [PR #6](https://github.com/deadinside28/bloodborne_pc/pull/6)
were adapted and built locally. Details and validation are in
[the comparison report](WINDOWS_PR6_REVIEW.md#adaptations-implemented-and-validated).
The native suite now passes 76/76 CTests. An additional production texture-cache
Vulkan readback test passes with threaded recording enabled and disabled.

The lifecycle-flag experiment used the same independent outdoor-save source,
1920x1080, upscaling off, current VRAM budget policy, and three repetitions of
`camera-attack-route.json`. Host input was excluded. Before starting each route,
the character was placed against the same obstacle and one standing R1 animation
was exercised. Each process started with a new user directory and no copied
pipeline cache. Menu/loading/scene setup happened before measurement; pass 1 is
not a measurement of the complete cold startup. No builds or other GPU tests ran
during the measured phases.

| Build / directory | Pass | Guest flips/s | p99 ms | Compiles | Frames >50 ms |
|---|---:|---:|---:|---:|---:|
| Before / `perf-pr6-before-01` | 1 | 41.10 | 50.48 | 49 | 20 |
| Before | 2 | 45.75 | 34.30 | 0 | 0 |
| Before | 3 | 46.45 | 34.50 | 0 | 3 |
| Lifecycle flags / `perf-pr6-flags-01` | 1 | 51.70 | 34.38 | 49 | 10 |
| Lifecycle flags | 2 | 54.55 | 33.91 | 0 | 2 |
| Lifecycle flags | 3 | 58.57 | 33.58 | 0 | 0 |
| All four adaptations / `perf-pr6-integrated-01` | 1 | 49.24 | 34.75 | 49 | 4 |
| All four adaptations | 2 | 53.79 | 34.05 | 0 | 1 |
| All four adaptations | 3 | 58.01 | 33.68 | 0 | 0 |

The two shader-warm passes improved by 19.2% and 26.1% in this before/after
experiment. This is an observed result on this route, not a universal causal
gain: rendering/input timing, temperature and background work can differ between
processes. p99 remains around 33.6 ms, so these results do not establish locked
60 FPS. The local GPU heap stayed near 2.90-2.92 GiB; sampled process working set
was higher in the flags run (warm peaks around 3.96 GiB versus 3.53-3.57 GiB).
Separate runs do not establish a memory leak or a RAM reduction.

All three before phases completed and their summary was regenerated from raw
data. That process's diagnostic stop exceeded the profiler's 90-second Windows
driver teardown wait; the process subsequently disappeared before the next game
launch. Compilation started after its measured phases, while teardown was still
pending. The flags run completed automatically with status 0 and a 13.13-second
teardown wait. Both source saves remained byte-for-byte unchanged.

The all-integrated repeat also completed with diagnostic status 0 (51.20-second
driver teardown), no fatal log lines and an unchanged source save. Its warm
passes measured 53.79 and 58.01 flips/s, with p99 34.05 and 33.68 ms. Peak working
set was 4.20 GiB and local GPU heap 2.94 GiB. The texture changes did not remove
the scene's visible foliage/geometry artifacts. Image refresh and upload byte
totals remained equal on this route, so this run does not demonstrate reduced
texture uploads. The correction's GPU-content behavior is covered separately by
the readback tests.

The texture correction is validated for GPU-content preservation and genuine
CPU writes, rather than an assumed FPS gain. Image upload byte counters count
guest mip ranges queued for copies. These ranges must not be described as VRAM
allocation or measured PCIe traffic.

To test reopening an existing shader cache, the profiler accepts
`--pipeline-cache out/perf-pr6-flags-01/user/cache`. It copies that directory into
the new test user's cache, hashes the source before and after, and never mounts
the source. `cache-source-verification.json` records whether it stayed unchanged.
For example, after confirming the prior process is gone:

```powershell
python .\tools\windows\profile_game.py --source-save out/windows-shad-save-20261005-020108/user/savedata/1/CUSA00207/SPRJ0005 --pipeline-cache out/perf-pr6-flags-01/user/cache --output out/perf-pr6-warm-new --route tools/windows/camera-attack-route.json --passes 2 --ready-timeout 1200
```

Inspect gameplay before creating `ready.trigger`, as described above. Warm-cache
reopening is a separate validation condition, not an equivalent cold-build FPS
comparison. Standing R1 tests do not validate enemies, death/reload or travel.

The actual reopening run `out/perf-pr6-warm-01` copied the 870-file cache from
the flags-only run into a fresh user directory, then loaded the outdoor save.
Both 30-second passes completed without fatal log lines or new shader/pipeline
compiles: 54.65 and 54.86 flips/s, p99 34.04 and 34.16 ms, and respectively 2 and
0 frames over 50 ms. Peak sampled working set was 3.87 GiB and local GPU heap
3.05 GiB. Diagnostic stop returned status 0. Source save and cache verification
both report unchanged. This verifies reuse of an existing cache across processes;
the separate SRT regression test explicitly exercises cached null-pointer fault
recovery, which this gameplay run did not log.

Final hashes also verify the original shadPS4 save (18 files), outdoor source
copy (18 files), source cache (870 files), and base/update executables against
their recorded hashes. Evidence: `out/pr6-review/final-source-verification.json`.
The remaining performance target is the recurring missed 16.7 ms frame deadline,
including CPU preparation and host-copy waits, alongside the visible rendering
artifacts. Existing warm hitches and gameplay outside this route remain open.

## Windows render deadlines and copy experiment

The Windows `Common::AccurateSleep` path opened an ordinary waitable timer on
every call. It now reuses a high-resolution auto-reset timer per native thread,
closes it at thread exit, rounds relative waits up to 100 ns and rechecks a
monotonic deadline after a wakeup. APC interruption still returns false with
remaining time. The present thread's deadline sleeps use this helper with a
standard sleep fallback on error; `AccurateTimer` uses the monotonic clock.

The full build passes 77/77 CTests (`out/pacing-build.log`). The new Windows
render-wait test checks short waits do not finish early, past/zero deadlines,
per-thread timer lifetime and APC interruption followed by a normal wait. It
reported zero handle delta, median overshoot 438 us and p99 1739 us for 120 waits
of 2.083 ms. These timings describe that host run; they are not upper bounds or
an FPS improvement. Non-Windows execution was not tested.

The existing `BB_VBLANK_HZ=0` mode runs internal vblank at 480 Hz; setting
`BB_FPS_LIMIT=60` caps guest flips separately. It is an experiment rather than
a change to the default 60 Hz guest vblank. With the pre-timer executable and
copied cache, `out/perf-pacing-60-01` completed three 30-second outdoor passes:

| Pass | Guest flips/s | p99 ms | New compiles | Frames >50 ms |
|---|---:|---:|---:|---:|
| 1 | 51.74 | 40.90 | 0 | 7 |
| 2 | 57.32 | 23.53 | 0 | 0 |
| 3 | 57.10 | 29.68 | 0 | 1 |

The quantization into 33 ms intervals became less prominent, but locked 60 FPS
was not established. Diagnostic exit returned 0 after an 89.64-second driver
teardown. Build work started only after the measured phases completed.

`out/perf-pacing-copy-ab-01` used the timer build and the same copied cache,
resolution, route and 480 Hz internal vblank / 60 FPS cap. The runtime's existing
disabled-optimization mask alternated between 0 (small copies on the recording
thread) and 524288 (batched copy-pool path). Each switch waited 500 ms outside
measurement, and the runtime log confirms the switches. No other GPU tests or
builds ran during measurement. One `nvidia-smi` hardware query ran during pass 2;
it observed 82 C and 83% device utilization, and can itself add diagnostic load.

| Pass / copy path | Guest flips/s | p99 ms | New compiles | Frames >50 ms |
|---|---:|---:|---:|---:|
| 1 / recorder | 55.92 | 31.38 | 0 | 0 |
| 2 / pool | 52.18 | 31.55 | 0 | 1 |
| 3 / recorder | 53.87 | 31.75 | 0 | 2 |
| 4 / pool | 52.51 | 24.23 | 0 | 0 |

Moving the small copies to the pool did not improve FPS in this experiment, so
the default remains the recorder path. The combined timing run does not isolate
a universal gain from the timer change. All four phases completed without fatal
log lines; source save and shader cache hashes remained unchanged.

Profiler controls now include `--vblank-hz`, `--fps-limit`, and one
`--optimization-masks` value per pass. `session.json` records the pacing and mask
list; `phases.json` records each applied mask. `--minimal-diagnostics` keeps frame
CSV and memory sampling while disabling detailed operation/stall timers, to
measure closer to ordinary play. `--upscaler fsr3 --upscale-preset quality`
explicitly tests the existing FSR 3.1 path: a 1280x720 scene at 1920x1080 output.
Upscaling defaults to off. These are separate conditions and must be labeled
when comparing FPS; reducing scene resolution does not prove a native-render
optimization. Combat, animation timing and area changes still need validation.

`out/perf-native-minimal-01` retained native 1920x1080, the recorder copy path,
copied cache and 480 Hz internal vblank / 60 FPS cap, with detailed frame
diagnostics disabled. Its three passes measured 54.46, 55.89 and 55.71 flips/s;
p99 was 36.76, 23.75 and 24.63 ms, with 2/0/0 frames over 50 ms and no new
compiles. Peak sampled working set was 4.14 GiB and local heap 3.00 GiB. This
condition did not establish locked 60 FPS. Source save/cache remained unchanged,
there were no fatal lines, and diagnostic exit returned 0 after 13.65 seconds.

## Intro queue recovery and FSR Quality repeat

The first FSR Quality attempt (`out/perf-fsr-quality-01`) ended with status 139
before gameplay. The host fault resolves to `AvPlayerQueue::Pop` called by
`AvPlayerSource::GetAudioData`: its empty check ran before taking the mutex, so
intro shutdown could clear the queue between the check and `front()`. Queue
size and conditional pop now take the mutex, and audio/video readers consume
optional frames without exposing an unlocked reference. A regression test
exercises concurrent producers, consumers and clear operations. The full native
build passes 78/78 CTests (`out/avqueue-build.log`). This fixes the identified
queue race; it does not establish complete media-player thread safety.

The corrected executable loaded the outdoor save and completed
`out/perf-fsr-quality-02`, with 60 Hz guest vblank, a 60 FPS cap and detailed
frame diagnostics off. Logs confirm a 1280x720 FSR 3.1 scene at 1920x1080 output;
object motion is enabled by the existing upscaler path. The source cache is the
same native-render cache as earlier, so it does not contain every FSR variant.

| Pass | Guest flips/s | p99 ms | New compiles | Frames >50 ms |
|---|---:|---:|---:|---:|
| 1 | 50.84 | 34.19 | 15 | 3 |
| 2 | 53.95 | 25.84 | 0 | 0 |
| 3 | 55.06 | 23.90 | 0 | 0 |

No fatal lines occurred; diagnostic exit returned 0 after 36.21 seconds.
Source save and cache hashes remained unchanged. Peak sampled working set was
4.28 GiB and local GPU heap 3.29 GiB. This does not establish locked 60 FPS or an
FSR speedup over native rendering: render mode, object-motion work and vblank
settings differ from the native 480 Hz experiment. Existing foliage/geometry
artifacts are still visible in captures.

The immediate-flip loop also reset each vblank deadline to `now + period`,
accumulating event-processing and wakeup lateness. Its deadlines now advance
from the previous slot, with overdue backlog discarded after a long stall and
the anchor reset during pause or the ordinary timer path. Default vblank and
frame-limit settings are unchanged. Build evidence: `out/vblank-anchor-build.log`
(78/78 CTests); gameplay results must be assessed separately.

The existing overlay counter reports smoothed present intervals. `Insert`
opens the overlay; enable `Счётчик FPS в углу` and close it to see the top-right
counter. Windows launches can also use `-ShowFps`, or `BB_SHOW_FPS=1` when
starting the executable directly. The profiler accepts `--show-fps` and records
its state in `session.json`; it defaults to off for comparison runs. Overlay
presentation rate and guest flip CSV measurements should not be assumed identical.

The anchored-deadline repeat `out/perf-fsr-anchor-01` retained FSR Quality,
60 Hz vblank, the 60 FPS cap and the same source cache. The overlay counter was
enabled, so this is not an isolated deadline-only A/B comparison. Three passes
measured 51.58/52.67/53.46 flips/s, p99 40.02/25.70/27.48 ms, 15/0/0 new compiles
and 3/0/0 frames over 50 ms. No fatal lines occurred, source save/cache remained
unchanged, and diagnostic exit returned 0 after 58.20 seconds. No FPS gain is
established by this repeat. Display-buffer readbacks precede the host swapchain
overlay and therefore do not include its counter.

Profiler options `--prep-workers` (0..16) and `--pipe-spin-us` (0..10000) expose
the renderer's existing helper controls; `session.json` records both. They do
not change runtime defaults. This host has an i5-13420H, eight physical cores
and twelve logical processors; automatic preparation uses six workers plus a
scanner, and automatic draw-recorder idle spin is 200 us. A reduced-helper
experiment uses four workers and 50 us, with native 1080p and the existing
480 Hz vblank / 60 FPS cap. Both helper controls change together, so any result
must be described as a combined condition rather than attributing it to one.

`out/perf-native-helpers-01` completed with 53.37/52.43/53.13 flips/s,
p99 31.95/28.51/33.56 ms, zero new compiles and 0/2/3 frames over 50 ms.
There were no fatal lines, source save/cache remained unchanged and diagnostic
exit returned 0 after 35.04 seconds. No improvement was demonstrated over the
earlier automatic-helper native run, so runtime helper defaults are retained.

`out/perf-native-gpu-timing-01` enabled GPU timestamps and detailed diagnostics
for one 30-second native 1080p route with automatic helpers. It measured 53.11
flips/s, p99 33.01 ms, no new compiles and one frame over 50 ms. Source save/cache
remained unchanged and diagnostic exit returned 0 after 49.86 seconds. Timestamp
instrumentation changes recording and adds overhead; this is a diagnostic run.
The selected full windows report 17.68-18.58 ms/frame **including** 0.71-2.72 ms
between-submission GPU idle. Larger contributors include full-size G-buffer
passes, copy-shader barriers/dispatch, and depth/color image downloads and
uploads. These are measured labeled segments, not an independent CPU profile or
a claim that idle time can all be removed. Window selection uses preceding
memory-report timestamps with a one-second margin; evidence is
`out/perf-native-gpu-timing-01/gpu-timing-analysis.json`.

### Correction to minimal-diagnostics measurements

The earlier `--minimal-diagnostics` runs set `BB_FRAME_STATS=0`. This stopped the
main frame report and operation timers, but four renderer/preparation checks
tested only whether the variable existed, and scheduler wait timers ran
unconditionally. Consequently those historical runs still collected drain-site
and wait statistics, and printed helper reports. Their raw FPS measurements
remain valid for that partially disabled diagnostic condition; they are not
fully diagnostics-off baselines.

Those checks now use the existing common `BbStats::enabled` predicate, and
wait timers use the same gated timer as operation timers. Frame CSV counts and
memory sampling remain available separately. Tests exercise disabled (`0`),
enabled (`1`) and unset flags with concurrent operation/wait counters. The full
native build passes 81/81 CTests (`out/stats-gating-build.log`). Native gameplay
performance with the corrected gating requires a separate repeat.

`out/perf-native-stats-off-02` loaded the outdoor scene with corrected gating,
and its log contains no frame/pipe/preparation/helper diagnostic reports.
The process ended before any route phases were recorded; its exit status was
not captured. This attempt provides no valid FPS comparison. A separate hash
audit in `validation-status.json` verifies the source save and cache unchanged.

### Offline v0.1 register scan adaptation

The package's libc++ word-based dirty-register scan is now adapted; captured
register deltas use the same traversal. The native build passes 81/81 CTests,
including sparse ordering and register payload checks. Full comparison and
other candidate integrations are in [the package review](WINDOWS_OFFLINE_REVIEW.md).

`out/perf-reg-scan-before-01` and `out/perf-reg-scan-after-01` use the same outdoor
save/cache sources, native 1080p output, internal vblank 480 Hz, frame cap 60,
automatic helpers and corrected `--minimal-diagnostics`. Each process runs three
30-second camera/R1 passes. FPS overlay and GPU timestamps are off. All passes
compile zero shaders; both source save and cache hashes remain unchanged.

Before: **54.41/54.59/55.34 FPS**, p99 **25.60/25.87/24.93 ms**, 0/1/0 frames
over 50 ms. After: **55.40/54.86/55.73 FPS**, p99 **25.12/28.26/26.72 ms**,
0/0/0 frames over 50 ms. Aggregate rates are **54.74 vs 55.29 FPS**, about +1.01%.
This single sequential pair does not establish a repeatable improvement, and
p99 does not improve consistently. It also does not demonstrate the performance
effect of diagnostic gating alone. Locked 60 FPS remains unverified.

Both processes reach the same outdoor scene and finish all phases without fatal
log lines. The before process exceeds the 15+75 second shutdown allowance and
the profiler terminates only its own process; disappearance is verified before
the next launch. The after process returns 0 after 26.18 seconds. This does not
prove the register change resolves intermittent shutdown delays. Summary and
termination limitations are in `out/offline-v01-review/game-comparison.json`.


### DLSS Super Resolution integration

The actual Vulkan backend, HDR input formats, capability fallback, resource
lifetime and raw Windows NGX shutdown ABI are integrated. Native validation
passes 87/87 CTests plus separate actual DLSS and texture-cache GPU tests.
Usage and test limits are in [Windows DLSS](WINDOWS_DLSS.md).

`out/perf-dlss-quality-02` uses the outdoor save and independent warm-cache source,
1920x1080 output, 1280x720 DLSS Quality input, internal vblank 480 Hz, 60 FPS cap,
automatic helpers and corrected minimal diagnostics. FPS overlay, GPU timestamps
and CPU thread sampling are off. The three 30-second camera/R1 passes measure
**44.09/49.05/50.29 FPS**, p99 **42.89/33.82/29.70 ms**, shader compiles **15/0/0**
and frames over 50 ms **7/3/3**. NGX creates the actual DLSS feature, without FSR
fallback. The process returns 0 after a 6.87-second teardown; source save/cache
verification passes. Existing foliage/geometry artifacts remain.

This does not improve on the earlier native 54.9-55.7 FPS samples. They are
sequential runs, not an isolated, repeated provider-only A/B study; no DLSS boost
or locked 60 FPS is claimed. Default upscaling stays off. The model's Quality
preset K has higher performance cost according to NVIDIA's SDK definitions.
Performance preset or different model versions require separate measurements.

ThinLTO is now explicit (`build-windows.ps1 -Lto`) and can be built separately in
`out/windows-lto`. Per-thread CPU accounting (`profile_game.py --cpu-threads`)
reports Windows execution time, not call stacks or waits, and adds diagnostic
overhead. Neither option is silently enabled in default FPS measurements.


### ThinLTO experiment

The Windows `-Lto` option explicitly requests ThinLTO before creating the root
runtime targets, so memory and synchronization also participate. Unsupported
explicit requests fail at configuration. `out/windows-lto` is separate from the
default `out/windows`; `lto-flags-verification.json` confirms `-flto=thin` on
runtime and renderer objects. Both final builds pass 87/87 CTests. The LTO DLSS
and texture-cache GPU readback tests also return 0. Build/test evidence is in
`out/dlss-integration-review/native-build-final.log`, `lto-build-final.log` and
`lto-gpu-validation.json`.

A single ordered pair uses the same current code, outdoor save, independent
warm-cache copies, native 1920x1080 output, 480 Hz internal vblank, 60 FPS cap and
automatic helpers. Each run completes three 30-second camera/R1 passes with
corrected minimal diagnostics. FPS overlay, GPU timestamps and thread CPU
sampling are off. Start captures show the same character position and HUD.
No shaders compile during any phase; source save/cache checks pass.

| Build | Pass FPS | Pass p99 ms | Frames >50 ms |
|---|---|---|---|
| Non-LTO | 56.67 / 53.56 / 56.81 | 26.57 / 31.14 / 25.33 | 1 / 4 / 1 |
| ThinLTO | 53.32 / 53.62 / 54.95 | 27.65 / 25.74 / 27.37 | 0 / 0 / 0 |

Aggregate guest flip rates are **55.68 vs 53.96 FPS**, about **-3.08%** for LTO.
The sample provides no average-FPS benefit. Fewer long frames in this one pair
do not establish a repeatable frame-pacing improvement; order, power/thermal
conditions and nondeterministic scene activity are not controlled. LTO remains
opt-in, and stable 60 FPS is not achieved. Both processes exit 0, but diagnostic
teardown takes 45.32/59.43 seconds, so shutdown delays remain unresolved.

Evidence: `out/perf-dlss-native-control-01`, `out/perf-lto-native-01` and
`out/dlss-integration-review/lto-game-comparison.json` (including binary hashes).
Existing rendering artifacts remain. These are short route samples, without
full combat, transitions or long-session validation.


### Opt-in thread CPU accounting

`out/perf-thread-cpu-01` runs the same native outdoor camera/R1 route for one
30-second phase with `--cpu-threads` added. The tool snapshots threads and queries
accumulated Windows kernel/user execution time every 0.5 seconds, without
suspension or call stacks. Samples crossing phase boundaries are excluded;
percentages therefore slightly undercount edge intervals. The denominator is
one logical CPU's phase duration, not total-machine capacity.

| Thread | CPU execution ms | Percent of one logical CPU |
|---|---:|---:|
| shadPS4:GpuCommandProcessor | 21000.00 | 69.55 |
| bb:DrawRec | 20203.13 | 66.91 |
| bb:VkRecorder | 14671.88 | 48.59 |
| Unnamed (TID 26008 in this run) | 14421.88 | 47.76 |
| GXRenderThread | 4328.13 | 14.33 |

These are thread-wide execution totals, which can include active spinning and
driver calls. They do not identify a slow function, prove which thread is on the
critical path or measure waits. The draw/command-recording paths are candidates
for subsequent stack-based CPU profiling; synchronization changes need their
own evidence. Sampling adds overhead, so this run is not used in the non-LTO/LTO
FPS comparison. Evidence is `thread-cpu.csv` and `summary.json` in the run folder.
The sampler closes queried thread handles on exit and after the diagnostic run.
Its phase-boundary/late-name aggregation test and actual Windows sampler smoke
check pass; Python profiler validation now has two tests.

The accounting run returns 0 after 31.52 seconds of diagnostic teardown. Source
save and cache verification both pass. Its 53.00 FPS sample is diagnostic only,
with zero shader compiles; sampling cost has not been isolated in an A/B test.

### Offline-package 720p parity investigation (2026-10-06)

All cases copy the same outdoor fountain save and pipeline-cache source, use
DLSS Native AA, 1280x720 output, Mailbox, internal vblank 480 Hz, host limit 120,
Skip Intro, and disabled motion blur/chromatic aberration. Two 20-second camera
passes follow a loading/warmup allowance; captures verify the same character,
HUD and fountain in the startup and package runs. Original saves/cache remain
unchanged. Evidence lives in `out/offline-performance-02/<case>/`.

| Case | FPS pass 1 / 2 | p99 ms pass 1 / 2 | Method |
|---|---|---|---|
| before-live-02: previous EXE/catalog, live 720p | 57.07 / 59.06 | 31.70 / 25.99 | Guest-flip CSV |
| after-live-01: new catalog, live 720p | 53.50 / 56.14 | 30.90 / 26.87 | Guest-flip CSV |
| after-startup-01: new catalog, startup 720p | 58.69 / 59.39 | 26.70 / 25.67 | Guest-flip CSV |
| native-sema-startup-01: native semaphore registry | 60.78 / 59.11 | 25.50 / 25.46 | Guest-flip CSV |
| worker-idle-startup-01: package helper priority | 59.61 / 59.61 | 26.08 / 23.82 | Guest-flip CSV |
| worker-background-startup-01: previous helper policy, same EXE | 58.20 / 58.46 | 29.55 / 23.68 | Guest-flip CSV |
| retail-startup-01: same final build, 5056 MiB pool | 56.38 / 55.73 | 25.53 / 25.03 | Guest-flip CSV |
| package-startup-02: actual downloaded EXE, 5056 MiB pool | 72.27 / 72.73 | unavailable | Mean of logged five-second windows |

Our first pass compiles 9 pipelines in these cases; second passes compile zero.
The package compiles during load and logs zero compiles during the measured
windows. Frame diagnostics are off in our CSV cases and on in the package;
its windows straddling the first five seconds of each phase are excluded, leaving
three samples per pass. Methods and memory-pool strategy differ. These ordered
runs do not isolate thermal/order effects or establish repeatable percent gains.
The small semaphore/helper differences therefore are not a stable-60-FPS claim.
Reducing our pool capacity alone does not explain the package's advantage.

Our runs return 0 through the diagnostic stop path. The package does not honor
our stop trigger and is terminated after the allowance (status 1); this is not
normal game shutdown. Its first attempt with an eagerly committed 9152 MiB pool
fails with Windows error 1450 before gameplay and is excluded. `before-live-01`
also is excluded: scripted menu input arrived before the initial UI was ready.

The final build keeps native semaphores, updated 01.09 FPS++ definitions and
CPU-idle preparation helpers. `BB_HELPER_PRIORITY=background` retains the former
resource-background scheduling for comparisons. The runner now connects saved
`live_resolution=0` to actual guest startup patches and recalculates after a
restart; output resolution and internal resolution remain separate controls.
See [the second package review](WINDOWS_OFFLINE_REVIEW.md#second-local-review-and-integration-2026-10-06)
for functional tests and commands. The remaining throughput gap requires CPU
function/wait profiling, particularly Windows memory protection/mapping and
guest synchronization; those are candidates, not measured causes.

## Region-table contention investigation (2026-10-06)

The next comparison identified an internal runtime bottleneck: the region table's
Windows winpthreads rwlock. Its shared path serializes reader accounting through
a mutex/event implementation, despite the renderer's many concurrent readers.
Stack samples connect it to GPU range queries, page protection and EOP backing
writes on the command processor, DrawRec and VkRecorder threads. The last path
also delays ordered copy signals, appearing as host-copy waits in renderer stats.

`src/runtime_memory.c` now uses a native SRW lock for this internal table on
Windows. Existing shared/exclusive nesting depths and table-generation cache
invalidation remain in place. Guest-visible mutex/rwlock exports and the memory
mapping backend are unchanged. Linux retains its checked pthread implementation.
SRW acquisition must not recurse; the depth guards bypass nested acquisitions,
including recovery from a protected guest output while a writer is queued.
See [Microsoft's SRW documentation](https://learn.microsoft.com/en-us/windows/win32/Sync/slim-reader-writer--srw--locks).

WPR CPU recording was unavailable in this session (`0xc5585011`: profile policy
could not be enabled). An external diagnostic sampler instead briefly suspended
the busiest threads, retrieved their Windows contexts, and unwound their PE
frames with DbgHelp. These are **wall-clock stack observations**, including waits,
not ETW CPU execution samples or measured function execution times. Profiling
runs are excluded from throughput baselines. The initial frame-pointer-only
walker was unreliable for Windows frames and its caller output is discarded.
The later StackWalk64 trace, symbolized with the archived exact pre-change EXE,
observed `ZwWaitForSingleObject` with a `pthread_rwlock_rdlock` ancestor in
31/443 command-processor, 40/443 DrawRec and 65/443 VkRecorder observations during
its in-phase sample window. It reports no context/resume failures. These samples
establish the wait path; the subsequent controlled EXE comparison tests its cost.

Artifacts are in `out/offline-performance-03`. The fixed-camera cases all load
independent copies of the same fountain save/cache, warm for at least 18 seconds,
and measure two stationary 20-second phases. The old EXE is the exact build before
the SRW change, with the same updated FPS++ catalog and native semaphores. Both
our versions average 1114.6–1114.7 draws/frame, with zero measured pipeline
compiles. This avoids the different camera trajectories/load counts seen in
earlier moving-camera comparisons.

Settings: scene/output 1280x720 at startup, DLSS Native AA, jitter/object motion
enabled, chromatic aberration/motion blur disabled, Mailbox, uncap FPS++, host
limit 120. The harness initially requests the FPS overlay off. The two SRW test
configs were subsequently saved with `show_fps=1`, 13.1 and 28.2 seconds before
the first measured phase respectively; the baseline remains `show_fps=0` and the
package ends with `show_fps=1`. That overlay difference is not treated as an
identical-setting comparison; the baseline repeat explicitly enables the counter
from launch and is still substantially slower. Subsequent cases enable it from
launch rather than depending on an overlay change during warmup.
Other persisted rendering choices match.
Our EXEs use the sparse 9152 MiB pool; the downloaded
EXE needs its eagerly committed pool limited to 5056 MiB on this machine. Our
frame diagnostics are off, with guest-flip CSV; the package's diagnostics are on,
with five-second FPS windows and the first five seconds of each phase excluded.
Those timing methods are not identical. All source save/cache hashes remain
unchanged. The package is forcibly terminated after testing, not cleanly exited.

| Fixed-camera case | FPS pass 1 / 2 | p99 ms pass 1 / 2 |
|---|---|---|
| `baseline-static-startup-01`: old internal region lock | 64.40 / 64.12 | 23.64 / 20.99 |
| `srw-static-startup-01`: native region lock | 70.29 / 71.24 | 15.47 / 14.71 |
| `package-static-startup-01`: actual downloaded EXE | 71.90 / 72.07 | unavailable in CSV |
| `srw-static-startup-02`: repeat native region lock | 71.36 / 72.04 | 15.02 / 16.84 |
| `baseline-counter-static-startup-02`: old lock, counter on from launch | 61.79 / 62.30 | 21.93 / 21.69 |

The counter-on repeat confirms the old EXE's deficit: about 62 FPS, versus
71–72 FPS in the new EXE and package. The initial counter-off baseline was about
64 FPS. This brings the new build close to the package in this scene. These
ordered comparisons do not prove equivalent
performance throughout the game or establish a thermally controlled gain.
NVIDIA temperature/clock/utilization samples accompany every run; they are not
hardware power/thermal controls. Full Windows build and all 87 CTests passed,
including concurrent memory operations, aliases, protection, and nested-reader
fault recovery with a queued writer.

The final EXE also completed the original 20-second moving-camera route twice
with the FPS counter explicitly enabled from startup:
`srw-counter-camera-startup-01` measured 71.84 / 73.23 FPS and p99 19.12 / 17.25 ms.
It compiled 9 pipelines in pass 1 and none in pass 2. This demonstrates the
improvement is not limited to the stationary camera, but occasional frames
still exceed the 16.67 ms target. These results do not claim stable 60 FPS in all
areas or combat.

Post-change diagnostic confirmation (`srw-counter-static-diagnostics-profile-startup-01`):
the command processor, DrawRec and VkRecorder each have zero observations of
the former `ZwWaitForSingleObject`/`pthread_rwlock_rdlock` chain in 439 in-phase
StackWalk64 samples per thread; the sampler reports zero failures. This does
not assert that the threads never wait: DrawRec now frequently waits for actual
GPU synchronization, while VkRecorder often waits for recording work. Renderer
reports around the stationary phases show host-copy waits about 1.5–1.6% and
command-thread idle 0.3–0.4%, versus roughly 15–18% and 7–13% respectively in
the pre-change diagnostic run. The post-change sampled run is excluded from
FPS baseline comparisons. All source save/cache hashes remain unchanged.

The finished Windows renderer is `out/windows/bb-game.exe`. Reproduce the tested
resolution/pacing choices with the existing persistent effect settings:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -UserDirectory out/windows-play-user -OutputResolution 1280x720 -ResolutionMode startup -Upscaler dlss -UpscalePreset native -PresentMode Mailbox -FpsPatch uncap -FpsLimit 120 -ShowFps -Timeout 0
```

The measurements above disable chromatic aberration and motion blur in each
private test ini. The command preserves the user's saved effects; it does not
silently impose those test choices. Other locations/combat remain unmeasured.
