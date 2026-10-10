# Windows integration of upstream 0.4

Upstream: tag `0.4`, pinned to
`daa71654083b42ea0730b3f0fecee2fc20942d02`.
See [the release](https://github.com/deadinside28/bloodborne_pc/releases/tag/0.4)
and [upstream change notes](https://github.com/deadinside28/bloodborne_pc/blob/0.4/docs/CHANGES_0.4.md).

## Scope

The renderer now includes upstream parallel Vulkan recording, ordered direct
segments, asynchronous submissions, range-specific host-copy waits, backing
readback for aliases, texture collection changes, shader-cache recovery and
replacement, camera/depth association, and the camera-motion shader fixes.
Windows DLSS, FG, Reflex, keyboard/mouse bindings, window modes, English overlay,
launcher settings and mod support are retained.

Windows adaptations include protected-view backing reads, portable thread and
fault diagnostics, atomic settings/cache replacement with `MoveFileExW`, and
Reflex window-message forwarding without exposing Vulkan headers to SDL code.
Negative projection Y now produces a positive FG camera field of view.

FG option changes, error shutdown and swapchain resize/HDR recreation are
serialized with Vulkan queue submissions. A valid real flip lacking an unused
guest token gets a fresh presentation token, rather than disabling/recreating FG.
These fallback tokens do not measure guest input/simulation latency. Redundant
host-vblank re-presents are skipped while FG is generating.

The Windows loaders retain normal ASLR and DEP but opt out of high-entropy
bottom-up allocation because guest code packs stack addresses into 40 bits.
An unexpected out-of-range stack still fails immediately. The Native AA test
exposed this intermittent startup failure before the loader flag was corrected.

The game check accepts upstream's exact unmodified 1.09 loaded-image hash and
the exact already-tested Windows 1.09 image with the Lance 60 FPS patch. Unknown
modified executables still fail the check; SFO metadata alone is insufficient.
The additional patched image requires CUSA03173 / 01.09 metadata.

Linux DMA-BUF/experimental PC guest memory and Linux trap-based diagnostic hooks
remain unavailable on Windows and fail closed. They are optional, not enabled
by this integration. Upstream's new IME overlay and a validated FSR 4.1.1 provider
are outside this Windows integration.

## Validation and deployment

Original sources and the active runtime/settings were backed up under
`out/upstream04-20261010/`. The candidate runtime is isolated in
`out/upstream04-candidate/`; integration alone does not replace the launcher's
active executable. Source game files and original saves are not modified.

After validation, the exact final smoke-tested EXE was deployed to
`out/windows-fg-game/bb-game.exe`, SHA256
`babd8663416156775eea236c23cadbc9031f24272f39d006a42e592629d72f87`.
The launcher uses that directory already. Its executable/preferences, active
`bbport.ini`, mod choices and all runtime DLLs were hash-verified unchanged.
Deployment evidence is `out/upstream04-20261010/deployment-result.json`;
the prior game/probe EXEs and deployment manifest are in `runtime-before/`.
The final exact-binary 10-second camera smoke test measured 37.77 rendered /
75.41 presented FPS and 30.10 ms p99, with a clean exit and unchanged source save.

Matched saved-scene tests use 1920x1080 output and window, DLSS Balanced,
borderless, Immediate presentation, uncapped FPS, Reflex Boost, a copied save,
10-second warmup and 20 seconds of camera oscillation. These are base-game
tests without the user's enabled asset mods. They do not traverse the stairs
or fight either boss, and cannot establish that those particular drops are fixed.

Before integration, FG off measured 54.27 rendered FPS and 21.18 ms p99.
The original 2X FG run failed with Vulkan `ErrorDeviceLost` after entering
gameplay; it is invalid performance evidence.

The first 0.4 FG-off candidate measured 53.29 rendered FPS and 21.65 ms p99,
with a clean exit and unchanged source save. This short sequential comparison
does not demonstrate a speed-up. FG 2X still produced `ErrorDeviceLost`, including
after skipping redundant vblank re-presents; that guard alone is insufficient.
Serializing FG option changes with submissions allowed the next Balanced 2X
run to exit cleanly, although missing guest tokens still caused repeated FG
recreation. Fresh presentation tokens then removed that observed cycling.

| Candidate, same copied scene | Rendered FPS | SDK presented FPS | Guest p99 ms |
| --- | ---: | ---: | ---: |
| FG off, initial 0.4 build | 53.29 | n/a | 21.65 |
| FG 2X Balanced, queue fix only | 33.15 | 69.53 | 109.14 |
| FG 2X Balanced, token fix, trace off | 37.60 | 75.28 | 32.91 |
| FG 2X Native AA, token fix, trace off | 31.20 | 62.47 | 36.53 |

All four rows completed cleanly with unchanged source saves. These are short,
sequential observations, not controlled thermal benchmarks. The 2X output is
generated presentation, not 75 FPS game simulation. FG still has substantial
GPU overhead and does not establish stable 60 rendered FPS.

The separate final Reflex trace observed 3,240 presented guest tokens with no
missing/duplicated marker stages, SDK status zero and retained focus. A prior
trace was inconclusive because merged stdout/stderr spliced a Vulkan warning
into one marker. The test helper now separates error output and rejects failed
traces in `measurement_valid`. Trace runs are excluded from FPS comparisons.
Driver input timestamps remain zero; end-to-end latency is not certified.

All 97 Windows CTests pass (including the supported-game-image check's ten
cases). Additional actual Vulkan checks pass for six camera
motion cases (sky rotation/translation, jitter cancellation, matching/stale
object vectors), color/depth/stencil resolution roundtrips at 4K/1080p/360p,
texture upload/CPU-write detection/GPU readback, and ordered scheduling across
three workers with 1 KiB segments and interleaved direct commands. Logs are in
`out/upstream04-20261010/`. These checks do not validate proprietary FG execution
or generated-image quality.
