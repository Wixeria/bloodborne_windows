# Windows port status

October 10: the [final 0.5 Windows integration](WINDOWS_UPSTREAM_0.5.md) is deployed
to the launcher's runtime, with 102 passing CTests and real Vulkan/gameplay checks.
The Windows host-import memory model is connected and available through
`-MemoryModel new`, and now defaults to new at the user's request. Launcher
Play > Runtime > Custom launch options accepts `-MemoryModel old` as an override.
A matched RTX 3050 scene measured
56.9 FPS old / 56.4 FPS new with FG off, and 38.8 rendered / 77.5 presented FPS
with old-model 2X FG. Atomic save replacement and additional-content handling
are adapted. The upstream native game menu remains disabled on Windows; use the
existing English Insert UI. Boss routes and new-model FG remain unverified.

October 10: upstream 0.4's renderer/runtime changes are adapted to Windows,
retaining DLSS/FG/Reflex, KBM, the English overlay and Rust launcher. All 97
Windows CTests pass, along with actual Vulkan scheduler, camera-motion,
resolution and texture-readback checks. Short FG 2X camera tests complete after
queue synchronization and token-gap fixes. See the
[0.4 integration review](WINDOWS_UPSTREAM_0.4.md) for results and limits;
the Gascoigne stairs and Blood-starved Beast arena are not yet validated.

Earlier milestones (October 5 onward):

The complete Windows x64 Gnm/VideoOut renderer and shader translator now build
into `out/windows/bb-game.exe`. The CPU diagnostic loader remains available as
`out/windows/bb-probe.exe`. Guest memory, threads/TLS, synchronization, clocks,
files/saves, offline services, RTC, SDL3 audio/gamepad and ATRAC9 are integrated.
Requires Windows 10 version 1803 or newer.

**The first playable area is verified on Windows.** The supplied CUSA03173
base and 01.09 update render character creation and Iosefka's Clinic at
1920×1080 on this host's NVIDIA GeForce RTX 3050 6GB Laptop GPU.
The test character `WindowsHunter` walks in the clinic and the camera rotates.
The in-game Exit Game command writes the character save and returns to the title.
A separate process reloads WindowsHunter in the clinic and accepts movement input.
Combat, physical audio/controller output and long sessions still need testing.
DLSS Super Resolution has now passed actual GPU and outdoor gameplay checks;
other advanced upscalers and broader image-quality validation remain pending. Unknown imports continue to stop explicitly.

The full renderer suite passes **87/87 CTests**, the earlier core suite **36/36**.
Independent Vulkan readback and Win32 swapchain checks also pass.
The latest native build/test log is `out/dlss-integration-review/native-build-final.log`.
Windows texture-cache thresholds now follow the current driver memory budget;
RAM, GPU heaps, shader compilation and frame intervals can be measured separately
with the save-copy profiler. See [measurements and reproduction](WINDOWS_PERFORMANCE.md).
An outdoor camera/R1 route now completes six 30-second passes, measuring
46.4–51.5 guest flips/s with about 4.28 GiB resident RAM and 3.02 GiB local GPU
heap usage. A forced-pressure repeat also completes, but an earlier 800 ms warm
hitch remains unresolved. Neither route verifies enemy combat or area changes.

Four corrections from PR #6 are now adapted: recorder/helper lifecycle flags,
cached SRT walker fault recovery, texture dirty hashes/upload baselines and
overlay widget evaluation order. The comparable all-integrated outdoor test
measures 53.8–58.0 guest flips/s in its two shader-warm passes; p99 is still
33.7–34.0 ms, so locked 60 FPS is not established. The production texture-cache
GPU readback test passes with recorder threading enabled and disabled. Existing
rendering artifacts remain. See [PR comparison](WINDOWS_PR6_REVIEW.md) and the
[measured results](WINDOWS_PERFORMANCE.md#pr-6-adaptation).

The Offline v0.1 package comparison also supplied a libc++ register-block scan
optimization. It passes the same 81 tests and completes three native outdoor
passes at 54.9–55.7 flips/s. Its sequential before/after difference is about 1%,
with no consistent p99 improvement; it does not prove a repeatable FPS gain.
DLSS is integrated and tested: Quality measured 44.1/49.0/50.3 FPS, so it stays
off by default. ThinLTO also passes 87/87 tests, but the separate gameplay pair measures
55.68 FPS non-LTO versus 53.96 FPS LTO; it remains opt-in.
Version-matched FPS++ remains a candidate. See [DLSS usage](WINDOWS_DLSS.md). See the
[package review and validation](WINDOWS_OFFLINE_REVIEW.md).

Windows deadline waits reuse a high-resolution timer, and immediate-flip vblank
slots now advance from their previous deadline instead of accumulating wakeup
lateness. The intro audio/video queue race is corrected; an FSR Quality repeat
loads gameplay and completes three outdoor passes (50.8/54.0/55.1 flips/s).
This does not establish a sustained 60 FPS mode. Add `-ShowFps` to the Windows
launch command to enable the existing top-right FPS/frame-time counter, or use
`Insert` and the overlay's `Счётчик FPS в углу` checkbox.

## Reproduce the milestone

From PowerShell in the repository:

```powershell
# First setup: pinned local toolchain, media and complete renderer dependencies.
.\build-windows.ps1 -Bootstrap -Gpu -Jobs 3 -Test -TestVulkan

# Subsequent full renderer builds, without downloading dependencies.
.\build-windows.ps1 -Gpu -Jobs 3 -Test

# Prepare and run the real game; no watchdog for an interactive session.
.\run-windows.ps1 -Gpu -GameDirectory 'D:\Games\CUSA03173' -PatchDirectory 'D:\Games\CUSA03173-patch' -SkipBuild -Timeout 0

# CPU diagnostics and independent GPU checks.
.\run-windows.ps1 -GameDirectory 'D:\Games\CUSA03173' -SkipBuild
.\run-windows.ps1 -BootImage 'out\windows-game\boot-linked.bin' -SkipBuild
.\run-windows.ps1 -VulkanOnly -SkipBuild
.\run-windows.ps1 -VulkanWindow -SkipBuild

# Core-only tests (separate directory keeps the full renderer build configured).
.\build-windows.ps1 -BuildDirectory 'out\windows-core' -Test
```

Use `-Toolchain <LLVM-MinGW directory>`, `-SDL3Directory <SDL3 x64 prefix>`, `-VulkanHeaders <checkout>`, `-FmtHeaders <checkout>` or
`-BuildDirectory <directory>` to override paths. Install CMake 3.24+ and Ninja,
or use Visual Studio's bundled CMake tools, which the script discovers through
`vswhere`. The compiler must target x86_64 Windows GNU, because guest exports use
`__attribute__((sysv_abi))`; plain MSVC is not currently supported.

Bootstrap pins LLVM-MinGW `20260922` (UCRT, x86_64) and SDL3 `3.4.18` and checks
both archives against the SHA-256 digests published in the official GitHub release
metadata. Vulkan-Headers `v1.4.309` and fmt `11.2.0` are checked against pinned
Git commits; fmt also supplies the renderer window compile check. Full GPU
bootstrap adds Vulkan-Headers `v1.4.350`, magic_enum `0.9.7`, robin-map `1.3.0`,
VMA `3.2.1`, xxHash `0.8.3`, Xbyak `7.31`, miniz `3.1.2`, Zydis `4.1.1`, glslang
`15.2.0`, Boost `1.88.0`, and the repository-pinned FSR/ImGui submodules. FFmpeg
uses the pinned BtbN LGPL shared `8.1.3-14-g330caae0c1` archive from release
`autobuild-2026-10-03-18-14`, SHA-256
`11a4b44bc69721274909619a779d82544c8b83a6557c5b1be92dce9a41a968be`.
Bootstrap verifies Git commits and archive hashes before building.
Dependencies install under ignored `out/toolchains/`, without administrator
rights or persistent PATH changes. The build script adds the compiler and SDL
DLL directories to PATH only while building/testing. Direct execution of test
EXEs use the DLLs staged beside them; no persistent PATH change is needed.

Outputs under `out/windows/` include `libbb-runtime-sync.a`, `libbb-runtime-memory.a`,
`libbb-runtime-thread.a`, `libbb-runtime-media.a`, `libbb-atrac9.a` and the test EXEs. These are independently
testable production runtime components, not placeholder game binaries.

## Implemented

The local base and update directories are remembered in ignored
`out/windows-local.json` (the `CUSA03173` base and its `CUSA03173-patch` update).
The base SFO reports `APP_VER=01.00`,
the update reports `APP_VER=01.09`, `CATEGORY=gp`, both `TITLE_ID=CUSA03173`.
The base eboot SHA-256 is
`6764938b23539d29c936bca9880fc4a774e7b0099ce31c7e8c4b0f8bd0befb80`;
the update eboot SHA-256 is
`22ccba99c1182247124b9cfa4c95472d2f2eea442eb61586b5ddec5b4e7cbe02`.
The update dump already contains Lance McDonald's 60-FPS patch, confirmed by
its title-screen credit. Zero additional code patch writes preserve that patch;
`--fps 30` does not undo modifications already in the supplied executable.
The sibling mods and backup folders are not enabled for these tests.

`scripts/game_overlay.py` combines base fallback files and update overrides
into a generated mount under the output directory. Both SFOs are checked for
matching title, update category and version. Case conflicts, file/directory
conflicts, links and output inside either source are rejected. Files are hard
linked, avoiding a second 31-GB copy and privileged symbolic links; all sources
and output must be on the same filesystem. Never edit generated mount files:
they share file data with their sources. The guest mounts `/app0` read-only.
The inventory fingerprint caches the mount and cached links are validated.
The real merged mount has 28,849 files and uses the update's 01.09 metadata.

`-PatchDirectory` selects an update explicitly. The remembered update is used
only with its remembered base; `-BaseOnly` disables it. Generated boot files,
logs and saves stay outside the supplied dumps. Test saves are isolated under
`out/windows-game-109/user/savedata/1/CUSA00207/SPRJ0005`: the SFO's
`INSTALL_DIR_SAVEDATA` is CUSA00207, despite the game's CUSA03173 title ID.

Windows speculative-reader recovery now uses a shared `BbFaultBuffer` and
non-SEH `_setjmp` with a null frame. The exception handler passes complete
`EXCEPTION_POINTERS` to the renderer's existing Windows context adapter, then
tries the thread-local recovery point. A new test covers 1,600 no-access faults
across eight threads and re-arming after a jump. The core suite passes 36/36.

- The existing PS4 mutex, condition-variable, semaphore and rwlock exports now
  use winpthreads on Windows, preserving the scoped NIDs, SysV calling convention
  and FreeBSD guest errors. The broken Windows branch of `runtime_mutex.c` was
  removed in favor of the shared implementation.
- Semaphore timeouts preserve their original monotonic deadline. winpthreads
  rejects `pthread_condattr_setclock(CLOCK_MONOTONIC)`, so Windows uses its relative
  wait API, recomputing the remaining interval after wakeups. Linux continues
  using monotonic condition variables.
- `gettimeofday` supplies the explicit guest layout via Windows FILETIME and
  timezone APIs, including the conversion from the 1601 epoch to Unix time.
- Audio uses a shared host clock helper. The Windows implementation reuses a
  high-resolution waitable timer per host thread and releases it through a
  pthread TLS destructor. Linux retains absolute `clock_nanosleep`, retrying only
  interruptions. Windows winpthreads' absolute monotonic sleep was found to fail
  during testing, so the audio runtime no longer calls it directly.
- SDL3 audio and pad code compile on Windows. Scripted controller input uses
  `SDL_GetPathInfo` for high-resolution modification timestamps instead of the
  Linux-only `stat.st_mtim`. The existing pad test uses Windows temporary files.
- The ATRAC9 decoder and AJM runtime compile; the recursive mutex initializer is
  selected per platform. Tests cover AJM context lifecycle, not actual game
  ATRAC9 decoding.
- Import-name lookup moved from `runtime.c` to `runtime_exports.c`, so
  media components can resolve production imports without linking the complete
  loader/runtime. Linux's existing `src/runtime*.c` build picks up both new files;
  its standalone pad test also links `runtime_clock.c`. Implemented export names
  have an additional generated NID table, retaining exact known library scopes;
  unknown/wrong-scope imports remain unresolved. Regenerate with
  `python tools/generate_runtime_nids.py` after changing export tables.
- Guest direct-memory allocation, shared aliases, flexible mappings, reservations,
  partial release/unmap, protection/query and batch operations now have a real
  Windows backend. GPU host helpers can write through the shared backing view
  while guest pages are protected; callbacks run outside the region-table lock.
- `MapViewOfFile3` with placeholder replacement supports the PS4's 16 KiB addresses
  and section offsets without ordinary Windows 64 KiB view alignment. Each PS4
  page has a separate shared view, so removing/replacing a page does not briefly
  unmap its live neighbors. Owned placeholders are reserved lazily in chunks of
  at least 64 MiB; foreign Windows heap/DLL allocations are skipped for ordinary
  placement and rejected for fixed mappings. Low image/stack allocations also
  use placeholder replacement and retain a guard gap.
- The backing section uses `SEC_RESERVE`; pages are committed on demand rather
  than charging the complete multi-GiB pool at startup. Released physical pages
  are zeroed before reuse. Flexible VMA splits now advance the physical offset
  correctly, fixing backing writes and release accounting after a middle unmap
  in the shared Linux/Windows code. Pool-size initialization is thread-safe.
- The Windows region table uses a statically initialized native SRW lock. GPU
  range queries, backing writes and page protection no longer pass through the
  winpthreads rwlock's reader-accounting mutex/events. Nested readers retain one
  shared acquisition, and an exclusive owner bypasses nested reads, so GPU fault
  recovery can reenter while a writer is waiting without deadlocking. Linux
  retains its checked pthread rwlock backend.
- Shared views support writable-to-executable transitions with exact guest
  protections and Windows instruction-cache flushing. A tiny x86-64 SysV guest
  function executes from both a shared RX view and a low RWX allocation in the
  native test; low allocations written after creation still need the loader to
  flush the instruction cache before execution.
- Guest pthread create/join/detach/self/exit and attributes now compile and run
  through winpthreads with the production SysV exports. Join and detach reserve
  their handle state under a lock, preventing two callers from joining the same
  native thread. Priorities/affinity remain recorded rather than scheduled.
- Windows retains its native GS/TEB. One process-wide `TlsAlloc` slot stores each
  thread's guest TCB. `runtime_thread_patch_tls_loads` rewrites prepared
  `mov rax,gs:[0]` instructions to the allocated slot's displacement, preserving
  instruction length and the original thread-pointer result. The x64 TEB offset
  is checked at compilation and the slot is checked against a real GS load at
  runtime. Linux keeps its original `ARCH_SET_GS` path.
- Main static TLS and linked-module TLS blocks are isolated per thread. Main TLS
  layout is frozen on first attach; module layouts are frozen on first use.
  Modules 2..7 support copied templates and zero-filled TLS. The native resolver
  moved from `runtime.c` into `runtime_tls.c`; production import dispatch uses it.
- Guest once/keys moved from Linux kernel services into
  `runtime_thread_keys.c`. Deleted/reused keys use generations so old per-thread
  values cannot leak into a new key. Destructors can rearm keys for four rounds,
  while static/module TLS remains accessible. Windows kernel import dispatch now
  resolves these portable contracts together with the shared kernel service backend.
- Normal return and explicit guest exit run guest destructors before reclaiming
  static/module TLS. Host-owned callback threads also register cleanup through a
  native pthread key. Windows uses non-SEH `setjmp` for guest exit: the test exits
  from raw machine code with no registered PE unwind entries. Low private TLS
  allocations are released back to placeholders, returning committed storage.

## Integrated loader, file and system services

- Images, import traps and entry arguments use memory below 1 TiB. Relocations,
  patches and linked module initializers run through the actual loader. Segment
  overlap, out-of-range relative pointers and unmapped TLS metadata are rejected.
- Fatal access/illegal-instruction faults include guest offsets; unresolved data
  imports include the import name. A native watchdog exits with code 124 on
  Windows; `--timeout 0` disables it. Windows command-line and boot/profile/patch
  paths preserve UTF-8 through a wide-character entry point and file APIs.
- Kernel clocks, CPU usage, process counters, sleeps, cryptographic host random,
  guest errno conversion and RTC run on Windows. Overflowing sleeps are rejected;
  TSC calibration is initialized once. Existing offline service contracts are
  shared, including one local user and disconnected network state.
- Files use Unicode `CreateFileW`, native 64-bit positions/stat timestamps,
  directory enumeration, rename/truncate/flush and real reads/writes. Guest
  directory records retain their FreeBSD layout. `/app0` and `/hostapp` are
  read-only; read-only save mounts enforce the same restriction. Guest paths
  reject parent traversal, Windows backslashes/drive streams and invalid endings.
- The reference Windows file backend serializes handle operations to preserve
  offset and lifetime across seek/read/close races. `pread`/`pwrite` restore the
  sequential offset. This favors correctness; game-scale I/O throughput is not
  measured. Linux retains its existing syscall backend.
- SaveData mounts, parameters, directory search/delete and SaveDataMemory use
  real files. The host adapter supports Unicode paths and 64-bit positions;
  Windows recursive deletion removes reparse points without following them.
  Tests only modify a newly created temporary tree, never existing user saves.
- `run-windows.ps1` runs the existing prepare/link/content/patch pipeline with
  no additional code patches and launches CPU diagnostics, or executes a prepared
  image/Vulkan check. It preserves the guest exit status. The complete game
  settings restart, mod overlays and launcher UI are still pending; the renderer
  overlay itself is compiled.
- Build output stages SDL3 and compiler runtime DLLs beside the EXEs. The native
  presentation executable was verified with only the Windows system PATH.

## GPU host integration

The actual renderer SDL window source compiles as `bb-gpu-window-port` on
Windows. Native-handle extraction is shared with `bb-vulkan-window.exe` and
passes the real HWND/HINSTANCE to the Win32 surface code. The presentation test
creates a hidden window, acquires swapchain images, clears and presents three
frames, then destroys its Vulkan/SDL resources. It is not a game rendering test.

GPU helper affinity/background scheduling and write-log register extraction
now use Windows APIs/`CONTEXT`; diagnostic reads use `ReadProcessMemory` so an
unmapped address does not cause another crash. These actual helper sources
compile and execute in `gpu-platform-test`. The complete renderer is linked
statically into `bb-game.exe` on Windows because PE DLLs cannot defer unresolved
runtime symbols to the executable as the Linux shared library does. The existing
Linux build remains shared. Win32-specific ports include thread/process profiling,
native file offsets, font assembly sections, Vulkan waits, libc++ bitset/atomic
compatibility and narrow MinGW headers for the upstream FSR sources. glslang
builds natively and generates the host shader headers; FFmpeg AVPlayer is linked.

The first full renderer boot exposed TLS slot exhaustion after the graphics
driver initialized. The loader now reserves the direct guest TLS slot before
loading graphics; `thread-reserved-slot` exhausts the remaining direct slots
and still executes real patched guest TLS loads across eight guest threads.
The fatal exception/watchdog paths flush diagnostics and terminate the process
without driver DLL detach callbacks, avoiding shutdown delays with active threads.

Diagnostic display readback now supports native guest buffers as well as upscaled
buffers. `BB_PRESENT_DUMP_TRIGGER` plus `BB_DUMP_DIR` captures completed display
pixels without requiring a desktop screenshot. The native-buffer path restores
its Vulkan image layout after copying and leaves capture disabled unless armed.

## Thread/TLS limitations before game validation

The Windows loader calls `runtime_thread_patch_tls_loads` on every writable
executable span before protection/execution, after preparation/linking and other
code patches. Main-thread attachment is also performed for unlinked CPU images. Patched displacements belong to one process; do not save
them into reusable prepared images. Only the existing prepared initial-exec
`mov rax,gs:[0]` contract is supported; arbitrary FS/GS operations are not covered.

The implementation needs a native TLS index below 64. Exhaustion is detected
and rejected at early reservation; expansion slots would need a longer instruction rewrite. Direct TEB
slot access is a Windows x64 implementation dependency, not a stable public API;
the Microsoft TEB documentation recommends `TlsGetValue` and notes that the
layout can change. The startup checks protect this assumption, but additional
Windows versions still need validation.

winpthreads accepts a custom stack attribute but its native creation path uses
only the stack size. Windows therefore uses OS-managed stacks to preserve native
TEB/exception bounds, checking their upper address against the guest's 40-bit
range before attachment. Native test stacks fit on this host; an out-of-range
stack stops execution and would need an explicit low-stack implementation.
Stable thread handle records are retained until process exit, and released low
addresses are not reused. Thread churn and full-game scheduling/stack behavior
remain unverified.

## Memory limitations before game validation

The Windows memory backend has been tested with synthetic guest API calls and
the real game boot, character creation and first playable clinic. One view per 16 KiB page favors correct partial operations;
the overhead at full game allocation sizes has not been measured. `SEC_RESERVE`
pages cannot be individually decommitted: released pages are zeroed, but the
section's committed size retains its high-water mark until process exit. This
differs from Linux's hole-punch reclamation and needs game-scale memory testing.
The loader installs vectored exception dispatch, calls the GPU page-tracking
hook first when graphics is enabled, and reports fatal guest/host faults and
unresolved data imports. Actual renderer page tracking and speculative GPU
read recovery are integrated; the clinic session exercises real renderer write faults.
Recovery tests also cover real no-access loads; broader gameplay coverage remains pending.
The memory unit tests still use their own narrowly scoped recovery handler.

## Validation

`build-windows.ps1 -Gpu -Test` builds with warnings treated as errors for the
project runtime and reports **87/87 CTest tests passing** (including the GPU-only
register test). The core-only build passes **36/36**:

| Test | Checks |
|---|---|
| `fault-recovery-host` | 1,600 real no-access faults across eight threads; recovery can be re-armed without SEH unwinding |
| `game-official-update-overlay` | Five Python cases: update precedence, base fallback, source preservation, caching, invalid metadata/conflicts/output and damaged-cache rejection |
| `gpu-hle-thread-lifecycle` | Production HLE thread wrapper: 100 worker self-joins racing external Join/Stop, completed-worker cleanup and restart |
| `gpu-register-dirty` | Empty/full register sets, word boundaries, bounded ranges, reset and ascending block iteration |
| `gpu-platform-host` | Actual helper affinity/background scheduling, Windows register context and safe diagnostic reads |
| `gpu-stats-*` (3 cases) | Disabled, enabled and unset diagnostic flags with concurrent operation/wait counters |
| `gpu-accurate-sleep` | Monotonic deadlines, short waits, APC interruption and reusable timer handle lifetime |
| `gpu-avplayer-queue` | Conditional frame availability and concurrent queue producers, consumers and clears |
| `gpu-bind-helper-lifecycle` | Actual helper task handoff, enabled/disabled states and idle destruction |
| `gpu-srt-cache-fault` | Cached-first walker registration, actual null-pointer recovery and duplicate concurrent fault contexts |
| `loader-*` (16 groups) | Actual BBPROBE loading, entry/low arguments, main/module TLS, raw guest threads, relocs/patches, Unicode paths, strict/unknown imports, data/protection/illegal-instruction faults, watchdog and malformed images |
| `runtime-nid-table` | Generated implemented-contract NIDs match the source tables |
| `system-contracts` | Clock IDs/layouts, real sleeps, overflow rejection, process/thread CPU usage, errno, random, offline services and RTC |
| `runtime-*` (7 groups) | Existing callback/guard/libc/memory contracts, rwlock ownership/contention/timeouts and expected failure paths |
| `file-*` (4 groups) | Unicode I/O, offset preservation, >4 GiB seek, stat layout, directories, mount/path restrictions, 4,000 concurrent preads, save persistence/metadata/search/delete/RO mounts and blob bounds |
| `thread-tls` | Execute a patched thread-pointer load; eight guest threads isolate static/module TLS and errno while Win32, compiler TLS and winpthreads remain valid |
| `thread-lifecycle` | SysV create/join results, invalid handles, self-join and repeated join/detach rejection |
| `thread-keys` | Isolated key values, four destructor rounds, deleted-key generations and stale-value rejection |
| `thread-once` | Eight threads race 400 once calls; one callback runs and invalid states are rejected |
| `thread-attributes` | Stack-size round trip, bounds, detach validation and creation using copied attributes |
| `thread-join-race` | Two native callers join one blocked guest thread; exactly one succeeds and detach cannot steal its handle |
| `thread-detached` | Detached join rejection, guest destructor completion and subsequent native cleanup with the TCB already cleared |
| `thread-raw-exit` | Explicit guest exit from generated x86-64 code with no PE unwind metadata preserves the returned value |
| `thread-host-cleanup` | A native callback thread attaches, runs guest destructors and releases its TCB on host exit |
| `thread-cold-start` | Sixteen first-use host attaches race without prior slot/key/main-thread initialization |
| `thread-bad-main`, `thread-bad-module`, `thread-bad-index`, `thread-bad-reconfigure` | Expected nonzero exits reject invalid TLS metadata, out-of-range offsets and reconfiguration after use |
| `thread-reserved-slot` | Guest TLS works after other users exhaust the remaining direct native TLS slots |
| `thread-no-slot` | Expected nonzero exit rejects exhausted direct native TLS slots |
| `memory-direct` | Actual 16 KiB shared aliases, non-64-KiB section offsets/addresses, unmap independence and zeroed reuse |
| `memory-partial-direct` | Middle physical release removes only its aliases; query offsets and backing writes stay correct |
| `memory-flexible` | Middle unmap, flexible physical-offset splits, hole reuse and zeroing |
| `memory-regions` | Reservations, fixed/no-overwrite mapping, query layouts/canaries and split protections |
| `memory-batch` | Batch prefix completion and count layout when a later operation fails |
| `memory-gpu` | Reentrant GPU callbacks, backing writes through read-only/no-access aliases and real Windows protection faults |
| `memory-bounds` | Overflow/invalid argument rejection, preserved live mappings and direct-to-flexible replacement |
| `memory-execute` | Execute tiny x86-64 SysV guest code from shared RX and low RWX mappings; check alias write permissions |
| `memory-concurrency` | Four threads repeatedly allocate, alias, query and release physical memory |
| `memory-low` | Distinct allocations below 40 bits, invalid-size rejection, guard-page faults and private allocation release |
| `memory-foreign` | Fixed-map rejection and ordinary placement skip a real foreign Windows allocation without changing its contents |
| `memory-boundaries` | Private and shared mappings spanning separately reserved placeholder chunks |
| `memory-nested-reader` | A real guest query-output fault reenters GPU protection while a writer queues behind the query's reader lock |
| `sync-contracts` | Real thread contention; SysV exports; guest errors; ownership; recursive/static mutexes; condition broadcasts and timeouts; rwlock exclusion; concurrent clock waits; guest time layouts |
| `semaphore-lifecycle` | Counts, errors, stale handles and timeout cleanup |
| `semaphore-concurrency` | Waiter ordering and token accounting |
| `semaphore-cancel-delete` | Cancellation/deletion wakeups and waiter lifetime |
| `semaphore-priority` | Blocking/wakeup behavior of the priority approximation |
| `audio-dummy-driver` | Production audio exports, dummy-device pacing, volume/error checks and AJM context lifecycle |
| `audio-silent-clock` | The same production pacing without an audio device |
| `pad-virtual-controller` | Existing pad ABI test, touch coordinates, scripted input and overlay capture using a virtual SDL controller |

The memory concurrency and nested-reader tests each pass 100 repeated process
runs after the lock fixes. The concurrency runs cover 8,000 allocation/alias/
release cycles across four threads; nested-reader runs cover 100 real fault
recoveries with a waiting writer.

The thread TLS, join-race, raw-exit, host-cleanup, detached and cold-start groups
each pass 100 repeated process runs (600 stress runs in total).

The portable Python suites `test_prepare`, `test_link_libc`, `test_content`,
`test_patches` and `test_upscaler_assets` pass: **29 passed, 3 native-binary tests
skipped** (rerun after these changes). The full C runtime is linked into the CPU
loader and the tests above. Optional hardware checks both pass on the RTX 3050:
`bb-probe --vulkan-only` verifies a 4096-byte fill/readback; `bb-vulkan-window`
verifies Win32 surface creation and three swapchain presents. Hardware checks
are separate from the default CTest suite and need a Vulkan-capable desktop.

Full Python discovery is **not passing on Windows**: in the initial run, 82 tests
ran, with 28 skipped, 17 errors and 1 failure. Failures include mod overlays needing
privileged symlinks, Linux shell-launch tests without Bash/coreutils, and the
Linux AppImage Vulkan-driver path test. These are recorded porting gaps; the
passing foundation tests do not certify those systems. Linux build/run regression
testing was not available on this host.

## Real game evidence and logs

`out/windows-game-109/game-fixed.log` records the 01.09 character creation,
intro-video playback and skip, clinic loading, character movement, camera
rotation and in-game save/quit. Vulkan readback captures were converted from
BGRA to PNG and visually inspected under `out/windows-game-109/capture-fixed/`:

- `character-finish.png`: confirms the named character WindowsHunter.
- `clinic-before-movement.png`: first playable clinic with HUD.
- `clinic-after-forward.png`: character moved from the bed toward the wall.
- `clinic-after-camera.png`: changed position and camera angle.
- `save-quit-confirm.png` and `saved-title.png`: in-game save/quit returns to title.

Skipping the intro originally crashed with `thread::join failed`. The AVPlayer
worker could detach itself while another caller joined it. The production HLE
thread wrapper now retains the worker handle for an external join, serializes
joiners and requests stop without holding the lifecycle mutex. The intro skip
and first-area transition now succeed; `gpu-hle-thread-lifecycle` exercises the
race repeatedly. This shared fix is compiled on Windows; Linux regression
execution was not available.

The build and **71/71** CTest result is in `out/windows-game-109/build-final.log`.
`game-reload.log` records the separate saved-character restart test.
`capture-reload2/reload-menu.png` shows the restored clinic scene,
`reload-status.png` confirms WindowsHunter at level 10, and
`reload-movement.png` records movement after loading.
Older 01.00 menu evidence remains in `out/windows-game-gpu-capture.log` and
`out/windows-game-menu-final.log`, with captures under `out/windows-game/`.
These diagnostic sessions use a watchdog or are explicitly stopped after their
checkpoint; a requested watchdog timeout exits 124.

Open logs in the editor, or follow the latest test in PowerShell:

```powershell
Get-Content .\out\windows-game-109\game-reload.log -Tail 40 -Wait
```

To continue using the isolated test character on this machine:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -OutputDirectory 'out\windows-game-109' -UserDirectory 'out\windows-game-109\user' -Timeout 0
```

Future runs can save their console output explicitly:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -Timeout 0 2>&1 | Tee-Object -FilePath .\out\windows-game-109\game-interactive.log
```

CPU mode defaults to a 10-second watchdog; GPU mode defaults to 90 seconds.
`-Timeout 0` disables it. Graphics diagnostics use empty settings and upscaling
off by default; explicit `BB_CONFIG` / `BB_UPSCALER` overrides are honored and
restored after launch. Keyboard: WASD for movement, mouse/arrows for camera, I/K for
menu up/down, Space for Cross, Left Shift for Circle, Enter for Options, Insert
for the port's settings overlay. Mouse buttons: left attacks, right fires the
left-hand weapon, middle locks on; F8 releases/captures the cursor. Input settings
are live and saved in `bbport.ini`. Keyboard/mouse also works with a gamepad
attached. See [controls and validation](WINDOWS_INPUT.md). The clinic run has shader hitches; premature
texture-cache pressure has been corrected against the current VRAM budget.
The source's 60-FPS patch does not establish sustained 60 FPS.

The existing repository `user` directory was also tested through a separate
ordinary copy in `out/windows-save-test-20261005-015647/user`. All ten character
files (`userdata0000` through `userdata0009`) contain only zero bytes; the common
settings file is populated. The game therefore shows New Game / System / Log In,
with no Continue or Load Game. The source save files remain byte-for-byte
unchanged, verified against the 14-file SHA-256 manifest. Evidence and logs:
`out/windows-save-test-20261005-015647/save-test-report.json`, `game.log` and
`capture/title-ready.png`. The usable WindowsHunter character remains in the
separate `out/windows-game-109/user` test directory.

A populated shadPS4 save was then tested from the usual shadPS4 savedata location
(`%APPDATA%\shadPS4\home\1000\savedata\CUSA00207\SPRJ0005`).
An ordinary independent copy was placed under
`out/windows-shad-save-20261005-020108/user/savedata/1/CUSA00207/SPRJ0005`.
The native Windows game loads `w1x`, level 30, with equipment and inventory in
an outdoor scene. Scripted movement, camera rotation and R1 attack animation
work. This is a save import/load check and short gameplay sample; it does not
establish full combat correctness or sustained 60 FPS. Frame statistics vary
with scene and loading, and shader/texture hitches remain visible.

Evidence is in `out/windows-shad-save-20261005-020108/game.log`,
`save-test-report.json` and `capture/` (`loaded-area.png`, `character-status.png`,
`after-movement.png`, `after-camera.png`, `attack-animation.png`). SHA-256 checks
confirm all 18 original save files, including shadPS4 metadata, are unchanged.
The imported copy uses the runtime's local user ID 1; the source's shadPS4 user
ID 1000 is retained only in the original path. Its `sce_sys` metadata is copied
inside the save container; the runtime loads the character data without requiring
that metadata to be converted to its separate `.sce_sys/param.bin` format.

To use the imported test copy with the normal Windows runner:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -OutputDirectory 'out\windows-game-109' -UserDirectory 'out\windows-shad-save-20261005-020108\user' -Timeout 0
```

## Next milestones

1. **Broader gameplay:** leave the clinic, test combat, transitions and deaths,
   and measure memory pressure and frame pacing over longer sessions.
2. **Device and graphics validation:** verify physical audio/gamepad and advanced
   upscalers, and repeat save/load across additional areas.
3. **Launcher/mods/distribution:** implement a general mod overlay without
   privileged symlinks and distribution packaging. The renderer's ImGui overlay
   and Windows settings restart are integrated.

References used for the Windows implementation:
[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw),
[winpthreads condition-variable implementation](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-libraries/winpthreads/src/cond.c),
[winpthreads rwlock implementation](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-libraries/winpthreads/src/rwlock.c),
[winpthreads thread/stack implementation](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-libraries/winpthreads/src/thread.c),
[Windows TEB layout](https://learn.microsoft.com/en-us/windows/win32/api/winternl/ns-winternl-teb),
[CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw),
[SetWaitableTimer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimer),
[MapViewOfFile3](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile3),
[VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect),
[VirtualAlloc2](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2),
[VirtualFree placeholder operations](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree),
[UnmapViewOfFile2](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-unmapviewoffile2),
[CreateFileMapping / SEC_RESERVE](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfilemappinga),
[ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile),
[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew),
[SDL3 native window properties](https://wiki.libsdl.org/SDL3/SDL_GetWindowProperties),
[Khronos Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers),
[FFmpeg Windows builds](https://ffmpeg.org/download.html#build-windows),
[BtbN FFmpeg builds](https://github.com/BtbN/FFmpeg-Builds),
[LoadLibraryExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw).


## Optional Skip Intro and FPS++ patches

The runner now accepts `-SkipIntro` and `-FpsPatch preserve|60|90|uncap` for game
launches. They require detected `APP_VER=01.09`; other versions fail before patch
compilation. The compiler writes only the output `patches.bin`; the loader
applies it to relocated runtime memory. Base/update source files are preserved.
Uncap selects the bundled `Uncap FPS++` and sets `BB_VBLANK_HZ=0` (internal
480 Hz) plus `BB_FPS_LIMIT=0` (no present limit). The 60/90 modes set that host
limit accordingly. Launch-time environment overrides are restored on exit.

This user's `out/windows-local.json` now remembers `skipIntro=true` and
`fpsPatch=uncap` for its configured game. Explicit command-line values override
these defaults. For the supplied independent playing-save copy:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -UserDirectory out/windows-play-user -SkipIntro -FpsPatch uncap -Upscaler off -ShowFps -Timeout 0
```

To launch without these additional patches, use `-FpsPatch preserve
-SkipIntro:$false`. `preserve` leaves the supplied executable's existing 60 FPS
modifications in place; it does not restore a stock 30 FPS executable.
`-PrepareOnly` compiles the selected runtime patch file without launching.

## Render resolution patch

Use `-Resolution WxH` (for example, `-Resolution 1280x720`) to patch the
game's internal scene resolution and set the renderer's guest render target to
match. This uses the resolution patch template for the detected game version;
the current catalog supports the 01.09 executable. The host output stays at
its configured resolution and the renderer scales the guest image to it.
`-Upscaler` and `-UpscalePreset` are separate options for temporal upscaling.
Dimensions must be even and between 320x240 and 7680x4320. Resolution above
1080p may also enable the resolution patch's increased graphics heap sizes.

Example: add `-Resolution 1280x720` to the Windows launch command above.

The current bundled pair contains **174 writes / 710 bytes** (171 FPS++, three
intro writes). Their ranges are disjoint, actual ELF segments contain every
write, and the catalog rejects the same names for 01.00. This is the repository's
existing catalog, not the offline package's different 328-write revision.
The focused patch suite passes 11 tests. Actual game startup reports all 174
writes applied and `VideoOut: vblank 480 Hz, frame limit 0 FPS`, reaches the menu,
loads a copy of the current playing save and completes a short camera/R1 route.
The original playing save/cache are not mounted by this diagnostic test.

Evidence: `out/patch-activation-review/preparation.log`, `patch-audit.json` and
`out/perf-intro-uncap-01`. These checks do not establish a repeatable FPS boost or
full variable-framerate physics/combat correctness.


The short uncapped route measures 52.37 FPS, p99 34.98 ms and two shader
compiles. The current playing save has a different starting position/HUD from
the older capped comparisons, so this sample does not isolate patch performance.
Both source save and cache verification pass. Diagnostic shutdown exceeds the
15+75 second allowance; the profiler terminates only its own PID 23376, whose
exit is verified. A clean shutdown is not established. Details are in the run's
`validation-status.json` and `summary-review.json`. A separate preparation with
no explicit patch flags verifies the saved defaults produce the same patch hash
(`out/patch-activation-review/default-preparation.log`).

## Windows overlay settings and restart correction (2026-10-06)

The runner previously recreated `diagnostic-settings.ini` on every launch,
discarding saved effects, while the Windows restart callback only reported a
fatal unsupported-launcher error. Overlay settings now persist in
`<OutputDirectory>/bbport.ini`; an explicit `BB_CONFIG` is shared by both the
renderer and patch compiler. Existing diagnostic settings are migrated once.
The first-run upscaler default is stored in the file rather than forcing an
environment override over saved menu selections.

Apply and Restart Game saves settings and asks the runner for a new process
using exit code 75. The runner recompiles patches from the saved settings before
launching again. It retains the game, save, output and explicit scene resolution
arguments, while initial CLI upscaler/preset/counter overrides yield to the saved
overlay choices on restart. Direct EXE launches save settings and request a
manual launcher restart. A failed settings write leaves the game open and shows
an error. The Windows restart terminates the old process without running CRT
destructors or DLL detach callbacks while GPU workers are active.

Fixed scene sessions now also initialize the final output dimensions from the
saved output setting. `-Resolution 1280x720` still fixes the internal scene; a
configured 1920x1080 output is expected with DLSS.

Validation: a native harness linked to production `probe_restart.c` and
`bbport_settings.cpp`, launched through the real runner and patch compiler,
saved disabled chromatic aberration/motion blur, requested a restart, observed
a second process with both disabled, and preserved them through a separate
manual launch. It verified all selected effect and 1280x720 resolution bytes
in the resulting 242-write patch file. Evidence:
`out/overlay-restart-check/last-result.json` and its referenced case logs.
The 11 patch tests and four selected upscaler-settings CTests pass.

An actual DLSS launch loaded an independent copy of the playing save into the
area. `out/overlay-game-check/game.log` reports `Camera motion: scene render size
1280x720`, `Upscaler: dlss inputs 1280x720 -> 1920x1080`, and the corresponding
NGX dimensions; `capture/gameplay.png` records the loaded scene. Source save
verification passes, and test processes were stopped. Mouse-click validation of
the overlay restart button was unavailable because the Computer Use native pipe
could not connect; the native save/restart/launcher flow was exercised by the
harness instead. These checks do not cover every overlay control or establish
clean game shutdown.
