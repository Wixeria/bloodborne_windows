# Windows NVIDIA Reflex

Reflex is now separate from frame generation. The launcher and Insert expose
**Off**, **On** (default), and **On + Boost**, stored as `reflex=off|on|boost`
in the selected `bbport.ini`. Insert changes apply at the next guest input
boundary. FG requires at least On; an Off preference is retained and applies
again after disabling FG.

Use the already prepared NVIDIA runtime:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -BuildDirectory out/windows-fg-game `
  -OutputDirectory out/windows-fg-play -UserDirectory out/windows-play-user `
  -FrameGeneration off -Reflex on -ShowFps -Timeout 0
```

`-Reflex` overrides the initial INI setting for that launch. Omit it when using
the saved launcher/Insert preference. The runner detects the prepared runtime
in the build directory; `-StreamlineDirectory` can select it explicitly.

With FG off, only Reflex and PCL plugins are requested. The SM86 proxy and FG
resource copies are not used in this path. The official Reflex runtime is used
on this RTX 3050; the experimental SM86 bridge is still needed for FG on this
GPU. A build without SDK headers or a runtime without the checked DLLs falls
back to ordinary rendering and shows Reflex as unavailable in Insert. No
driver, HAGS, global NVIDIA settings, or system tools are changed.

## Frame association

The trace established one pad sample per `sceGnmSubmitDone` epoch, with about
twelve graphics submissions in the measured gameplay frame. Pad sampling and
graphics submission occur on different guest threads.

The runtime sleeps immediately before pad sampling, outside the pad mutex.
It allocates one Streamline token per sampled epoch and sends simulation-start
and controller-input markers there. The first GNM submission sends
simulation-end/render-submit-start once. Decoder tasks retain that token;
an ordered draw-pipe task transfers it to the scene/display pipeline without
a per-batch draw-pipe drain. The presenter sends render-submit-end after its
queue flush, then present-start/end with the same token. FG color, depth,
motion and HUDless input leases also retain the matching token.

These are **guest pad-to-GNM boundaries**, not a reverse-engineered hook at
the entry and exit of Bloodborne's entire CPU simulation loop. The reported
simulation interval must not be presented as total engine simulation time.
Startup frames can use the earlier renderer fallback until guest input timing
is established. Reused or blank display frames do not resend a token's markers.

Options and state queries are serialized separately from guest timing;
the presentation thread does not take the guest mutex while Sleep may be
waiting for presentation. Off still invokes the SDK's Sleep and marker APIs,
with the runtime configured Off. SDL's Win32 message hook handles PCL latency
ping messages and driver-controlled left-button flash requests without taking
the guest/pad mutex. These diagnostic paths still need external verification.

The API/UI behavior follows NVIDIA's pinned
[Streamline 2.14.1 Reflex guide](https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideReflex.md).

## Validation, 2026-10-09

Hardware: RTX 3050 6GB Laptop, existing driver/runtime. All game runs use a copy
of `out/windows-play-user`; the source save hashes are checked after each run.
The test oscillates the camera in the saved scene. 1080p output and borderless
1080p window, DLSS Quality, live resolution enabled, uncapped game/FPS limiter.

Initial FG-off On trace: `out/reflex-20261009-on-trace/summary.json`.
All **4,326 presented guest tokens** had exactly one ordered sequence of
Sleep/start/input/simulation-end/render-start/render-end/present-start/end.
No duplicate or misordered sequence was found. The run exited 0 and source
save hashes were unchanged. This log-heavy run is a structural check, not the
Off/On performance comparison.

Matched runs without the per-frame trace log:

| Mode | Real guest flips/s | Guest p99 frame time | Evidence |
|---|---:|---:|---|
| Off | 52.90 | 22.82 ms | `out/reflex-20261009-off/summary.json` |
| On | 53.46 | 21.15 ms | `out/reflex-20261009-on/summary.json` |
| On + Boost | 53.04 | 22.88 ms | `out/reflex-20261009-boost/summary.json` |

The three 20-second samples show no obvious On throughput regression. Their
small FPS differences are not evidence of a speedup. All used executable SHA256
`3722bdd3504dab52be4320c8ae70678050e6f1f396d57d657e1294d364347bed`.

FG 2X with requested Reflex Off correctly applied Reflex On:
`out/reflex-20261009-fg2x-trace/summary.json`. It measured 38.27 rendered /
76.53 actually presented FPS (SDK ratio 2.000), status 0, focused throughout,
exit 0, unchanged source save. All 4,309 presented guest tokens had exactly one
ordered marker sequence. The same-frame final/HUDless color check matched at
all 307,020 sampled scene pixels; these are input images, not generated pixels.
This run used per-frame tracing and is not a direct FPS comparison with the
untraced native runs.

NVIDIA reports `lowLatencyAvailable=1` and `latencyReportAvailable=1` and
returns up to 64 nonzero, ordered CPU/GPU frame reports. However,
`inputSampleTime` remains zero on this runtime, despite accepted controller
input markers. The logs retain the raw fields for further investigation.
**No certified keyboard/mouse input-to-photon latency reduction is claimed.**
NVIDIA's external Reflex Test Utility/FrameView verification and physical
click-to-photon measurement have not been performed. Those require separate
system setup; ordinary mode switching does not install them.

The C++ settings persistence/override tests and pad sampling lock test pass.
The SDK-enabled game builds, and the SDK-disabled Reflex/FG stubs compile.
Rust launcher model tests pass, including invalid Reflex mode rejection.

The final build includes relative-mouse flash event forwarding and disables
FG capture if Reflex fails. Its SHA256 is
`25b9226ed9e0b957d2150ce85b30ef093aac9fd9c9c23af4b532ff35e6273a89`.
Two additional checks of this exact binary passed:

- `out/reflex-20261009-final-fg2x`: FG 2X, On, status 0, focused, exit 0;
  37.24 rendered / 74.31 actually presented FPS in a 10-second moving-camera
  sample. This shorter run is not a matched before/after performance result.
- `out/reflex-20261009-official-only`: On with `version.dll`, `sl.dlss_g.dll`
  and `nvngx_dlssg.dll` absent from the isolated runtime. Reflex and PCL
  initialized, support/guest pacing/focus checks passed, exit 0, and no FG
  scene copies were made. Both runs preserved source save hashes.

The native launcher UI smoke check passed, including 330 controller hit-target
checks at three widths; the new Reflex row was inspected in the rendered FG
page image. Preferences and the live game INI were unchanged. Hidden-window
UI captures did not refresh and failed the smoke assertions; repeating with
a visible test window passed. This was a test-window visibility issue.

The tested game and standard release launcher were deployed to
`out/windows-fg-game/bb-game.exe` and
`out/windows-launcher/BloodborneLauncher.exe`. Previous binaries/manifest and
deployment verification are in `out/reflex-20261009-deployment`. Runtime DLL
pins, launcher preferences and the live `bbport.ini` were unchanged.
