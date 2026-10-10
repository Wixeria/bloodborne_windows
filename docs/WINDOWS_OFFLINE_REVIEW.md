# Offline v0.1 package comparison — 2026-10-05

Reviewed the local package
the independently downloaded Bloodborne PC Offline v0.1 package (build
`e090ac5e25fab486dd9e113e3a6203fda24b8241`)
and fetched its source commit, pinned as `origin/offline-v01-review`:
[e090ac5e25fab486dd9e113e3a6203fda24b8241](https://github.com/deadinside28/bloodborne_pc/commit/e090ac5e25fab486dd9e113e3a6203fda24b8241).
No checkout, merge, package launch or write to the downloaded package was performed.

## Relationship to the earlier PR

The source is 27 commits ahead of our base `a219d84`. Compared with the already
reviewed PR #6 head `bbb7f7231cfc53fa994a4215ff0ecff0b958c379`, its `gpu/` tree
is identical. Among `gpu/`, `src/` and `build.sh`, only `src/runtime_pad.c`
changed after that PR (launcher-selected gamepad). Subsequent commits primarily
add the launcher, packaging and logs. We previously adapted four PR corrections,
rather than merging its whole Windows implementation.

## Useful differences

| Area | Package / source | Our implementation and next use |
|---|---|---|
| Register-block scanning | libc++ uses 64-bit words and scans set bits | Our fallback tested all 1,664 blocks per pipelined draw. The word scan is now adapted through `RegDirty::ForEachBlock`; `RegDelta::Capture` uses that same traversal. |
| DLSS | `vk_dlss.cpp/.h` loads driver NGX and consumes the existing color/depth/motion/jitter path | Now adapted with capability checks, real image formats, failure fallback, resource lifetime and corrected raw Windows shutdown ABI. Actual GPU readback, recreation and outdoor gameplay pass; Quality does not increase FPS on this RTX host. |
| Build | MSYS2 CLANG64, libc++, LLD, LTO enabled by default | LLVM-MinGW/CMake/Ninja, `BB_LTO=OFF`, `BB_PGO=off`. ThinLTO is now opt-in via `-Lto`, including the root runtime. It is a separate build/performance experiment; no evidence the portable package used a trained PGO profile. |
| FPS patches | Updated FPS++ lists, including animation and foliage timestep corrections | Our supplied update already contains a 60 FPS patch. The user has now requested Skip Intro + Uncap FPS++; the existing bundled 01.09 pair is available through the Windows runner (174 writes). It is separate from the offline package's newer catalog. |
| Resolution | Non-1080p output defaults to a guest startup resolution patch (`live_resolution=0`) | Our tested FSR 1080p condition uses live host targets. Startup and live modes are different conditions; compare the same scene, dimensions and upscaler before attributing performance. |
| Launcher / distribution | Portable Python, native launcher, per-session logs, settings and model assets | Useful deployment work; it is separate from measured renderer speed. |

The FSR 4/4.1.1 renderer and FSR-Vulkan revision were already in our base source.
This package supplies FSR 4 v07 model assets and a DLSS model. Its DLSS backend is
Super Resolution; the inspected runtime does not implement frame generation.
The user-reported RX 9070 XT 4K Quality 100+ FPS result has not been independently
reproduced here. It cannot establish an equivalent RTX 3050 Laptop result or a
same-scene speedup over our builds.

## Local package crash: version mismatch

The package's `out/analysis.json` identifies `APP_VER=01.00`, SHA-256
`6764938b23539d29c936bca9880fc4a774e7b0099ce31c7e8c4b0f8bd0befb80`.
This is the same base executable we keep separate from the official update.
The 20:27:39 session log mounts only `ROMs/CUSA03173`, compiles `Uncap FPS++`
and applies 328 writes. `scripts/run_windows.py` does not supply `--app-version`,
so `scripts/patches.py` selects its default **01.09** patch definitions for this
**01.00** executable. The process then faults before gameplay.

Applying a version-specific patch to this different executable is a concrete
launcher defect and a strong crash candidate. The exact causal instruction has
not been established. The log does not benchmark a successfully running game.
Our runner reads the detected version and overlays the separate official 01.09
update when configured.

## Adapted register scan

The scoped adaptation preserves the existing native memory/runtime, scheduler
and cache corrections. Sparse, full, empty, cross-word and tail scans are covered
by the production register test, including independent sparse-pattern ordering
and captured register payloads. The native build passes **81/81 CTests**;
evidence is `out/offline-v01-review/reg-scan-build.log`.

The same outdoor save, warm-cache source, 1920x1080 native output, automatic
helpers, 480 Hz internal vblank, 60 FPS cap and corrected diagnostics-off setting
were tested in one sequential before/after pair, three 30-second camera/R1 passes
per process. Source saves and caches were copied independently and remained
unchanged. The FPS overlay and GPU timestamp instrumentation were disabled.

| Register scan | Pass FPS | Pass p99 milliseconds | Compiles | Frames >50 ms |
|---|---|---|---|---|
| Before | 54.41 / 54.59 / 55.34 | 25.60 / 25.87 / 24.93 | 0 / 0 / 0 | 0 / 1 / 0 |
| After | 55.40 / 54.86 / 55.73 | 25.12 / 28.26 / 26.72 | 0 / 0 / 0 | 0 / 0 / 0 |

Aggregate guest flip rate changed from 54.74 to 55.29 FPS (about +1.01%). A
single ordered pair cannot establish a repeatable gain, and p99 did not improve
consistently. **Locked 60 FPS is not achieved.** Both processes reached gameplay
and completed all measured phases without fatal log lines. The before process
exceeded the 90-second diagnostic teardown limit and was terminated by the
profiler; its disappearance was checked before starting the after process.
The after process returned 0 after a 26.18-second diagnostic teardown. The
register change does not establish a fix for the intermittent shutdown delay.

Evidence: `out/perf-reg-scan-before-01`, `out/perf-reg-scan-after-01` and
`out/offline-v01-review/game-comparison.json`. Existing foliage/geometry artifacts
remain visible. This route does not verify enemy combat or area transitions.

## Next experiments

ThinLTO now has an explicit separate-build option and passes 87/87 CTests plus
the actual DLSS/texture-cache GPU tests. A matched-settings sequential pair
measures 55.68 FPS non-LTO versus 53.96 FPS LTO; it provides no average-FPS gain.
It remains opt-in. See [performance measurements](WINDOWS_PERFORMANCE.md#thinlto-experiment). DLSS integration is complete: 87/87 CTests, real GPU readback,
resize/reinitialization and three outdoor passes at 44.1/49.0/50.3 FPS. The first
pass compiles 15 shaders; subsequent passes compile zero. Exit is 0 and original
save/cache hashes are unchanged. It remains off by default. See
[DLSS validation and commands](WINDOWS_DLSS.md). Compare guest startup
resolution and live resolution using the same upscaler and dimensions. Evaluate
FPS++ separately only after checking the exact executable version and existing
60 FPS modifications. Resolution and FPS++ are still candidates. None of these
changes demonstrates an explanation for the reported 100+ FPS result. Optional per-thread CPU
accounting is available in the profiler; it measures execution time without
call stacks or wait attribution and adds diagnostic overhead.


The first actual-game CPU accounting run identifies GpuCommandProcessor,
DrawRec and VkRecorder as the largest named execution-time users (69.55%, 66.91%
and 48.59% of one logical CPU). This selects profiling candidates; it is not
function/wait attribution or proof of an optimization. See
[CPU measurement limits](WINDOWS_PERFORMANCE.md#opt-in-thread-cpu-accounting).


## User-requested patch activation

Skip Intro + Uncap FPS++ are now enabled in the local Windows launch preferences
for the configured 01.09 game. The existing bundled catalog supplies 174 memory
writes; no ROM files or offline-package XML are replaced. The host present limit
is also removed. Version/range/output checks, 11 focused patch tests and actual
menu/save-load/short-route validation are recorded in
[Windows patch status](WINDOWS_PORT.md#optional-skip-intro-and-fps-patches).
Earlier non-FPS++ benchmarks remain historical; this activation is not a matched
performance comparison or evidence for the reported 100+ FPS result.

## Second local review and integration (2026-10-06)

The package now contains successful 01.09 sessions. In particular,
`logs/bbport-2026-10-06_00-30-03.log` selects startup-patched 1280x720,
DLSS Native AA (1280x720 input and output, quality mode 5, LDR), internal
vblank 480 Hz and a 120-FPS host presentation limit. The earlier 01.00 crash
analysis above describes the older sessions, not this working configuration.

New adaptations:

- The Windows runner consumes `live_resolution=0` for non-1080p output and
  computes guest startup dimensions from the saved output and preset. Before
  this correction only an explicit `-Resolution` supplied startup patches;
  the overlay's Disabled choice had no launcher effect. Live scaling can retain
  native 1080p post-processing targets even with a 720p final image.
- Initial upscaler/preset overrides participate in that calculation; an overlay
  restart recalculates from newly saved settings and clears stale startup
  dimensions when switching to live mode. Initial output/mode flags also yield
  to saved overlay choices on that restart. Native UI coordinates remain 1080p.
  TAA keeps its native guest targets. Windows Auto currently selects the
  startup path without a device-capability probe; the overlay explains this.
- `-OutputResolution`, `-ResolutionMode`, `-PresentMode` and `-FpsLimit` make
  the package's startup/output/presentation conditions reproducible. The actual
  Vulkan presentation mode is logged after checking driver support. Defaults
  preserve saved preferences and the existing uncap policy.
- Only 01.09 `60 FPS++`, `90 FPS++` and `Uncap FPS++` metadata were adapted
  from pinned commit e090ac5, preserving patch author credits and other patches.
  Uncap now has 328 writes; the tested effects/intro/startup-resolution combination
  has 399. Executable version and mapped-range validation remain required.
- Semaphore registry locks and waiter condition variables use native Windows
  SRW/CV primitives, following the package's host synchronization approach.
  Token ordering, cancellation, deletion and monotonic timeout accounting remain
  intact; Linux still uses pthreads. Guest mutex/thread ownership was not replaced.
- Draw preparation helpers use the package's CPU-idle priority policy. The old
  Windows background mode remains selectable with `BB_HELPER_PRIORITY=background`.
  That mode also reduces I/O/memory resource priority, unlike CPU-idle scheduling;
  see [Microsoft SetThreadPriority](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority).

Production Save/Load/restart testing through the real PowerShell runner and real
ROM patch compiler verifies four launches: 854x480 scene / 720p output, then
1280x720 scene / 1440p output, live 1440p with no stale render override, then
native 720p startup again. The earlier explicit-resolution restart harness also
passes. Evidence is `out/offline-performance-02/automatic-restart-result.json`
and `out/overlay-restart-check/last-result.json`. Patch tests pass 17/17; the
final native Windows build passes 87/87 CTests, including untimed semaphore wake.

Actual package EXE testing uses independent copies of the same fountain save and
cache, native 720p DLSS, Mailbox, 480 Hz vblank, 120-FPS host limit, intro skipped,
chromatic aberration and motion blur disabled. The package's eagerly committed
9152 MiB pool first fails with Windows error 1450; using the retail 5056 MiB pool
allows gameplay. Our pool is sparsely committed and uses 9152 MiB on this path.
Thus pool capacity and allocation strategy differ in this comparison.

The package logs 72.27 / 72.73 FPS in two short camera passes (three five-second
samples per pass, first five seconds excluded). It has frame diagnostics on and
no per-flip CSV support. This confirms the user's reported approximate range
in this scene, but is not an interchangeable metric with our CSV averages.
Our startup run before native semaphore changes measures 58.69 / 59.39 FPS;
after native semaphores it measures 60.78 / 59.11 FPS. Neither small sequential
pair establishes a repeatable native-semaphore FPS gain or locked 60 FPS.
Original save/cache hashes are unchanged in every run. The package was stopped
by process termination after the diagnostic allowance; this is not a clean exit.
Further helper-priority measurements and evidence are recorded in
[Windows performance measurements](WINDOWS_PERFORMANCE.md).

The final same-EXE helper comparison measures 59.61 / 59.61 FPS with CPU-idle
helpers versus 58.20 / 58.46 with the previous resource-background policy.
One sequential pair does not establish a repeatable gain. Matching the package's
5056 MiB pool in our final build measures 56.38 / 55.73 FPS and does not reproduce
its advantage. The successful automatic restart harness also includes initial
output/mode CLI overrides, verifying that saved overlay choices win afterward.

The remaining Windows memory-view strategy and guest synchronization differences
are candidates for profiling, not adaptations already validated here. The port
has not yet reproduced the package's 72-FPS gameplay throughput.

## Follow-up: internal memory-table lock (2026-10-06)

Function/wait sampling located a concrete bottleneck in our Windows region-table
rwlock. GPU range lookups, page protection and ordered EOP backing writes all
passed through winpthreads' shared-reader accounting, with waits observed on
the command processor, DrawRec and VkRecorder threads. The table now uses native
SRW shared/exclusive operations while retaining the nested-reader and exclusive
owner guards required by GPU fault recovery. Guest mutex/rwlock exports and the
sparse, individually mapped memory views are unchanged.

The new EXE now reproduces approximately the package's 720p throughput in the
tested fountain scene. Two stationary 20-second phases with DLSS Native AA:
old EXE with counter off 64.40 / 64.12 FPS, old EXE with counter explicitly on
61.79 / 62.30, new EXE 70.29 / 71.24 and repeated 71.36 / 72.04, downloaded
EXE 71.90 / 72.07. Our stationary cases compile no pipelines and average about
1114.7 draws/frame. A moving-camera route with the new EXE and counter on gives
71.84 / 73.23 FPS, with 9 compiles in the first pass and zero in the second.

These are local ordered measurements on the RTX 3050 6GB Laptop GPU. The package
uses logged five-second FPS windows versus our per-flip CSV; its committed pool
is limited to 5056 MiB, while ours is sparse 9152 MiB. Counter changes during
warmup are recorded, and the counter-on old-EXE repeat confirms its deficit.
All source save/cache hashes are preserved. This establishes scene-level parity,
not stable 60 FPS throughout all locations and combat. All 87 Windows CTests,
including memory concurrency and nested fault recovery, pass after the change.
Detailed methods, stack evidence and artifacts:
[region-table investigation](WINDOWS_PERFORMANCE.md#region-table-contention-investigation-2026-10-06).
