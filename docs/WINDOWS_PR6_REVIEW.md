# PR #6 comparison — 2026-10-05

Reviewed [PR #6](https://github.com/deadinside28/bloodborne_pc/pull/6), head
`bbb7f7231cfc53fa994a4215ff0ecff0b958c379` (9 commits, 72 files), against local
base `a219d841546b2ca28a3fefb11268ce7d5ca54192` **and the current uncommitted
Windows implementation**. The PR is open. Its head was fetched into
`refs/remotes/origin/pr-6-review`. The initial review below preceded integration;
the adaptations and subsequent validation are recorded at the end. No branch
merge or checkout was performed. Metadata and candidate diffs are saved in
`out/pr6-review/`.

The author's performance results use an RTX 4090 / i9-13900K; their reported
112–120 FPS clinic run is not a prediction for this RTX 3050 laptop. The PR also
documents unresolved rendering/loading problems and an unavailable Windows
`build.sh --test` path. Our 74 passing CTests do not validate their branch.

## First optimization candidate: per-command thread queries

In `vk_scheduler.h/.cpp`, the PR replaces repeated recorder
`std::jthread::joinable()` calls with an explicit `recorder_running` flag.
`vk_bind_helper.h` similarly changes `Available()` to read `running`. These
queries existed in our sources at review time, including `Record()` and
`IsRecordingDeferred()`, which are called repeatedly during rendering.

This is relevant to our actual toolchain, not only MSYS2. The bundled libc++
`__thread/jthread.h` implements `joinable()` as `get_id() != id()`. Inspection of
our staged `libc++.dll` locates `__libcpp_thread_get_id` at `0x1800a7cd0`, loading
the handle and jumping to imported `GetThreadId`.

A standalone diagnostic (`out/pr6-review/joinable_bench.cpp`) was compiled with
our LLVM-MinGW compiler, `-O2 -std=c++20`. Five passes of 500,000 calls on a live
thread produced:

| Pass | jthread joinable (ns/call) | Volatile flag (ns/call) |
|---|---:|---:|
| 1 | 495.0 | 1.0 |
| 2 | 350.9 | 1.1 |
| 3 | 338.6 | 1.0 |
| 4 | 337.4 | 1.0 |
| 5 | 321.7 | 1.0 |

Both noinline query functions returned true on all 1,000,000 checks per pass.
The volatile flag prevents eliminating the comparison loop. This establishes
call overhead, not total renderer cost or an FPS improvement. The next useful
experiment is a narrowly scoped flag change followed by the same outdoor route
with unchanged resolution, save source and compilation conditions. Check both
threaded recording and `BB_VK_RECORD_THREAD=0`, plus helper enable/disable paths.
Lifecycle flags must retain the stop/join behavior; this does not justify
removing synchronization.

## Other missing corrections

| Candidate | Difference found in our sources | Validation before adopting |
|---|---|---|
| Cached shader SRT walkers | `RegisterWalkerCode` does not ensure the fault handler is registered; its range start is set only when generating a walker. The PR uses a shared one-time registration and handles duplicate concurrent patch faults. | Warm shader-cache reload and concurrent null-pointer walker faults; inspect generated code range and patch handling. |
| Texture dirty hashes | `MarkAsMaybeDirty` hashes the whole guest image while `RefreshImage` hashes only initial pixels. Mip hashes are recorded only for already GPU-modified images. The PR uses matching ranges and records upload baselines. | Character preview/exposure, genuine CPU texture writes, retained GPU-written contents, queued upload counts and CPU hashing cost. |
| Overlay controls | `Store(value, v, ImGui::Checkbox(...))` and the analogous slider expression can read `v` before the widget modifies it. The PR evaluates the widget first. | Toggle/slider value persistence through actual overlay input. This correction can be extracted independently of its translation/DLSS changes. |
| FPS patch lists and wind timestep | The PR changes `patches/Bloodborne.xml`; our supplied update executable already contains a 60-FPS patch. | Inspect existing instruction bytes and patch overlap, version/region and animations before enabling additional writes. |
| DLSS | New NGX integration plus settings, device extensions and upscaler plumbing are absent locally. | Device/model availability, failure fallback, image quality in motion, sharpening and real GPU-bound performance. CPU submission overhead is the earlier target. |

The texture changes could affect both correctness and repeated uploads. At
review time no local gameplay measurement had been made. Our current VRAM
budget/accounting fixes and phase-aware profiling are independent additions and
must be retained.

## Pieces requiring separate evaluation

`src/win32_memory.c` maps larger views and protects per view, while our backend
uses separate 16 KiB views to keep neighbouring live mappings intact during
partial replacements. Larger views may reduce protection-call overhead. Its
partial unmap remaps surviving pieces and restores guest permissions while
queuing GPU invalidation; our backend snapshots actual 4 KiB permissions and
supports replacement rollback. This is a different mapping strategy, requiring
alias/partial-unmap/tracking/concurrency/failure tests rather than a file swap.
The PR also creates its pool with `SEC_COMMIT`; ours uses sparse `SEC_RESERVE`
commitment. Compare commit charge as well as resident RAM on this 16 GB host.

The intro-thread fix overlaps our existing work but has different ownership
semantics: the PR moves the thread under a lock and detaches self-stop; ours
retains ownership for external join and waits for concurrent joins. Our tested
implementation should not be overwritten wholesale. The PR's generic HLE
exception wrapper also needs per-entry return/error semantics reviewed before
adoption.

The setup program and MSYS2 build path are alternative deployment workflows;
our pinned toolchain/CMake runner already builds the native renderer. They do
not need to replace it to evaluate the renderer fixes. The PR's ThinLTO is
another isolated build experiment; current builds explicitly disable LTO.

Priority: measure the lifecycle-flag optimization first, then validate the SRT
cache, matching texture hashes and overlay control corrections independently.
Full-branch performance and gameplay remain untested on our hardware.

## Adaptations implemented and validated

Four corrections were adapted from yumlevi's PR for our LLVM-MinGW Windows
implementation: recorder/helper lifecycle flags, cached SRT fault registration
with serialized duplicate-fault patching, consistent whole-image dirty hashes
and initial mip upload baselines, and explicit widget evaluation before storing
overlay checkbox/slider values. Existing thread ownership, guest memory mapping,
VRAM budgets, upload counters and diagnostic timing remain in place.

The full native build passes 76/76 CTests (`out/pr6-integrated-build.log`). New
tests exercise actual helper enable/disable, task handoff and destruction, and
cached-first SRT registration, null-pointer recovery and concurrent duplicate
fault contexts. The concurrent SRT test dispatches contexts; it does not prove
arbitrary simultaneous execution while machine code is being patched.

An additional hardware test, `out/windows/texture-cache-vulkan-test.exe`, uses
the production Vulkan texture cache and GPU readback. Both ordinary recording
and `BB_VK_RECORD_THREAD=0` pass: initial uploads establish mip baselines,
unchanged guest bytes retain GPU-written contents, unrelated writes on the same
guest page leave the image unchanged, and a CPU modification beyond the first
8x8 pixels reaches the GPU. Logs: `out/pr6-texture-test.log` and
`out/pr6-texture-test-direct.log`. It is an explicit GPU test, not a CTest that
can run on every machine. The headless fixture refreshes images directly and
does not test rasterizer page-fault tracking.

After a full renderer build, reproduce the explicit GPU checks without running
the game at the same time:

```powershell
$env:BB_GPU_USER_DIR = [IO.Path]::GetFullPath('out/texture-cache-check/user')
$env:BB_UPSCALER = 'off'
Remove-Item Env:BB_VK_RECORD_THREAD -ErrorAction SilentlyContinue
.\out\windows\texture-cache-vulkan-test.exe
$env:BB_VK_RECORD_THREAD = '0'
.\out\windows\texture-cache-vulkan-test.exe
Remove-Item Env:BB_VK_RECORD_THREAD, Env:BB_GPU_USER_DIR, Env:BB_UPSCALER
```

Overlay expression ordering was inspected and compiled; actual widget input
and persistence have not been tested. No additional FPS patch, DLSS integration,
memory-backend replacement or intro-thread ownership change was applied.

The same outdoor camera/R1 route was measured before and after the lifecycle
flags, with separate copies of the same save. See
[performance results and limits](WINDOWS_PERFORMANCE.md#pr-6-adaptation).

Reopening the flags-only run's existing 870-file shader cache with the integrated
build also passed (`out/perf-pr6-warm-01`): gameplay loaded, both camera/R1 passes
completed with zero new shader/pipeline compiles, and diagnostic exit returned
status 0. Both the source save and source cache were unchanged. This is compatible
cache reuse, not proof that the gameplay run encountered the null-pointer fault
covered explicitly by the SRT test.
