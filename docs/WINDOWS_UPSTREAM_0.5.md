# Windows 0.5 integration review

October 10, 2026. Final **0.5** renderer/runtime changes are adapted to Windows
and deployed to the Rust launcher's existing runtime directory. The new memory
model is connected and tested in gameplay, and is now the Windows default at
the user's request. The launcher provides a custom-options field to select old.

Pinned upstream: `c7e3e55eff7c4a37fed314766d2180a9028c35db`.
[Upstream release](https://github.com/deadinside28/bloodborne_pc/releases/tag/0.5).
This supersedes the earlier 0.5-pre4 foundation checkpoint.

## Integrated changes

- Connected `BbLayer::GpuMemory`, page tables, VRAM mirrors, invalidation and
  guest write tracking to Windows rendering. The Windows backend imports
  committed 64 MiB ranges from the runtime's shared section using Vulkan host
  import and buffer device addresses, without Linux dma-buf or sparse rebinding
  for those ranges. CPU mappings keep their independent protection.
- Ported the validated 1.09 loader/allocator detours to Windows exception
  contexts, executable stubs and instruction-cache flushing. All six native
  detours installed in the new-model gameplay test.
- Integrated command-processor copy/label, predication, semaphore and GPU clock
  changes, asynchronous pipeline handling, residency changes and occlusion-query
  defaults. Preserved the Windows FG submission lock.
- Adapted shared write-trap ownership to Windows section-view boundaries;
  added real protection-fault and chunk-allocation regressions.
- Ported atomic saves through Unicode Windows filesystem helpers: sibling
  temporary files, flush before replacement, interrupted-write preservation and
  stale-temporary cleanup. Added an actual child-process abandonment regression.
- Integrated additional-content enumeration, damaged shader-extraction checks
  and the offline-choice skip patch. Preserved merged 1.09 game-folder handling.
- Retained Windows NGX/Streamline DLSS, FG and Reflex, the English Insert overlay,
  KBM bindings, window modes, mod overlays and Rust launcher.

Upstream native game Display/Effects menu detours remain disabled on Windows;
use Insert and the launcher. The Linux DLSS SDK bridge was not adopted. Linux
builds and AMD/Intel Windows gameplay were not tested in this integration.

## Validation

The main build and **102/102 Windows CTests passed**. Additional real Vulkan
checks passed on **RTX 3050 6GB Laptop GPU**, NVIDIA driver 617.14:

- Production runtime host import: 32 MiB, alignment 4096, nonzero buffer device
  address, three CPU/GPU round trips with the guest view still read-only.
- Recording order: 3 workers, 1 KiB segments and 3200 fills spanning worker,
  direct and submission boundaries.
- Paged shader copies: current source, mirror/guest writes, cross-page ranges,
  unmapped/out-of-range guards and mirror-only ownership.
- Portable 32/64-bit labels: high-before-low publishing, earlier GPU payload
  visible during CPU polling, GPU consumers and zero-valued signals.
- Upscaler support diagnostics and fallback checks.
- Deployed `bb-probe.exe --vulkan-only`: Vulkan submission and 4096-byte readback.

Logs: `out/upstream05-release-20261010/{main,ctest,probe,scheduler,translation,upscaler}.log`.

### Matched gameplay measurements

Three runs used separate copies of `out/windows-play-user`, the same Central
Yharnam church-door scene, DLSS Balanced, 1920x1080 output/window, Immediate
presentation, Reflex On, 8 seconds warmup and 25 seconds scripted camera motion.
These were base-game tests without the launcher's mod overlay.

| Memory model / FG | Real FPS | Presented FPS | Guest p99 frame time |
| --- | ---: | ---: | ---: |
| Old / Off | 56.94 | n/a | 21.40 ms |
| New / Off | 56.36 | n/a | 22.27 ms |
| Old / 2X | 38.75 | 77.50 | 30.24 ms |

FG-off FPS comes from guest flip timestamps. FG-on real/presented FPS comes from
steady Streamline statistics; its guest flip rate was 38.90 FPS. Presentation
ratio was 1.99997. SDK status and HUDless tagging passed at the expected
resolution. All runs passed validity checks, had zero invalid sampled Reflex
marker tokens and preserved the original save hashes. Saved base-frame captures
looked consistent between memory models.

Evidence:

- `out/upstream05-final-old-retry/{summary.json,session.json,game.log}`
- `out/upstream05-final-new/{summary.json,session.json,game.log}`
- `out/upstream05-final-fg2x/{summary.json,session.json,game.log}`

**No FPS improvement from the new model was measured in this scene.** FG still
reduces real rendering throughput. These short camera tests do not validate the
Gascoigne stairs or Blood-starved Beast fight, prolonged VRAM pressure, generated
frame artifacts or end-to-end input latency. New-model FG was not tested.
Reflex driver input timestamps remain unavailable; marker ordering is not a
certified latency measurement.

The FG-off diagnostic stop took roughly 90 seconds to release the process after
rendering stopped. These watchdog/termination paths do not establish normal
in-game quit behavior.

## Defaults and launch commands

Windows now defaults to new; `-MemoryModel auto` also selects new. `new` sets
`BB_PC_MODEL=1`; `old` sets it to `0`. The runner restores the previous environment
afterward. Unsupported host import still disables the in-place backend.
This is a startup option, not an Insert setting. Linux defaults are unchanged.

Test the new model with FG off:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -BuildDirectory out/windows-fg-game -OutputDirectory out/windows-fg-play -UserDirectory out/windows-play-user -MemoryModel new -FrameGeneration off -ShowFps -Timeout 0
```

In the Rust launcher, **Play > Runtime > Custom launch options** accepts
`-MemoryModel old` to select the previous model. Leave it empty for new.
Options are saved in launcher preferences, override matching generated launch
parameters and are passed as literal runner arguments. For example:

```text
-MemoryModel old -FpsLimit 90 -WindowMode borderless
```

Quote paths containing spaces. Malformed quotes, duplicate custom options and
unsupported runner parameters are rejected. This field accepts game launch
options, not shell commands. The launcher clears inherited `BB_*` variables, so
setting `BB_PC_MODEL` in its parent shell is not a launcher override.

The user subsequently reported that the previously mentioned visual/performance
problems were fixed. This is user feedback; the recorded automated measurements
above still cover only the church-door scene.

## Deployment and rollback

The initial runtime integration replaced `bb-game.exe`, `bb-probe.exe` and
`deployment.json` under `out/windows-fg-game`. The subsequent default/options
update also rebuilds and replaces `BloodborneLauncher.exe`. Launcher preferences,
active INI, DLLs, original game assets and original saves are preserved.

Gameplay-tested executable SHA256:
`1406567c88680724ade0a22e5ad19c62293654b3a3d6adec256cf38a7d68eeb5`.

Initial 0.5 deployment executable SHA256:
`893d2c2337cf43ce5f120bdb050dc797d002bb9ede0f7bad2cbf0be002739be2`.

The subsequent new-default update uses SHA256
`8d9649285b3ce29e54a7be182302599c7ef797fa2ec5946999411c3d91de8131`.
It changes only Windows memory defaults and adds the Rust launch-options field;
102 CTests pass with the new default. The no-override startup check confirms
host import and all six native hooks. It uses a copied test user, disables
upscaling/FG/Reflex to isolate memory startup and intentionally ends with watchdog
exit 124. This is not an FPS benchmark. Evidence and the previous runtime/launcher
backup are under `out/windows-launch-options-20261010`.
All 18 launcher model tests pass, including actual PowerShell argument binding.
Native UI checks typed the old-model override, saved a temporary preferences
fixture and cleared the field back to new at 1180x820 and 1000x680. Screenshots
were visually checked; active preferences and INI hashes remained unchanged.

The sole renderer change between measured gameplay and the initial deployment protects import-probe timeout
cleanup: pending GPU work must finish or report device loss before its resources
are freed. The final binary rebuilt, passed 102 CTests and the additional Vulkan
checks, and passed an eight-second new-model startup/import smoke. That smoke
intentionally ended with watchdog exit 124; it is not a fourth gameplay benchmark
or a clean-quit test. Evidence:
`out/upstream05-final-probe-smoke/{summary.json,game.log}`.

Previous 0.4 EXEs, manifest and settings snapshots are retained in
`out/upstream05-release-20261010/runtime-before`. Deployment checks and protected
file hashes are in the sibling `deployment-result.json`. Close the game before
restoring the backed-up EXEs and manifest; deployment does not change preferences.

Immutable merge snapshots are under `out/upstream05-release-20261010`.
Do not rerun their apply/preparation scripts.
