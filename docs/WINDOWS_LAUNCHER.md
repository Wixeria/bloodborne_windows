# Windows launcher

The English Windows desktop launcher is a native Rust application using
[GPUI Kit](https://github.com/longbridge/gpui-kit), pinned to 0.7.1 with Cargo.lock.
Its GPU-rendered UI uses the styled Sidebar, Button, Input, Select, Switch and
AlertDialog components. Cards and controller drawing belong to this application.
There is no webview or Tk frontend in the Rust executable.

The supplied Bloodborne rune is embedded as Windows icon resource 1 in both
BloodborneLauncher.exe and bb-game.exe. The shared assets/windows/bloodborne.ico
contains nine sizes from 16 to 256 pixels. GPUI loads the launcher icon from the
executable; the SDL game window uses the same resource for its large taskbar and
small titlebar icons. No adjacent image files are required at runtime. The game
resource is linked by CMake, and the launcher resource by its Rust build script.

The selected output folder's bbport.ini and existing launcher preferences are
shared with Insert and the earlier launcher. Ini merging, validation, save
import/backups and session supervision are implemented in Rust. The existing
PowerShell runner and Python 3 game-preparation scripts remain dependencies
when launching the game; Tk is no longer needed for the launcher.

Double-click launcher-windows.cmd from the checkout, or run:

    .\out\windows-launcher\BloodborneLauncher.exe

The compiled executable is out/windows-launcher/BloodborneLauncher.exe.
It finds the checkout above its executable; keep it there or in the project root.
Rebuild with Rust MSVC and Visual Studio C++/Windows SDK tools installed:

    .\tools\windows\build_launcher.ps1 -Test

The script builds a release executable by default; -Development selects a development
build. No Rust compiler is needed to run the built executable. The earlier Tk
frontend remains available explicitly with python launcher/windows_launcher.py.
The old C entry shim is retained as legacy source and is no longer built by
build_launcher.ps1. User folder/runtime preferences keep the same settings.json.

## Play

- Select CUSA03173 containing eboot.bin and sce_sys/param.sfo. A merged 01.09
  game needs no separate update. The launcher ignores an invalid remembered
  update directory and checks the selected title/version before launching.
- The port user folder contains savedata and cache. Existing saves remain in
  their selected folder. To copy an external shadPS4 save, choose Import and
  select its decrypted SPRJ0005 folder. The original stays intact; an existing
  destination is renamed to a timestamped backup before replacement.
- Settings/output folder selects the profile and generated launch files.
  Load selected profile switches the editable settings. Output and user folders
  inside the game dump are rejected.
- Executable folder contains bb-game.exe, bb-probe.exe and their DLLs. The
  launcher runs existing builds; it does not silently download runtimes or rebuild.
- **Runtime > Custom launch options** stores additional runner arguments. Windows
  uses the new memory model by default; enter `-MemoryModel old` to use the
  previous model, or leave the field empty for the new one. Options override
  matching launch selections; for example `-MemoryModel old -FpsLimit 90`.
  Quote paths containing spaces. Shell commands, malformed quotes and duplicate
  custom options are rejected. Save settings or launch to persist the field.

The custom-options path is checked by launcher model tests and an actual
PowerShell parameter-binding test. A native UI smoke types, clears and saves
options in a temporary preference fixture at 1180x820 and 1000x680 without
changing the active profile. Build the optional test harness with
`build_launcher.ps1 -UiValidation -OutputDirectory <test-folder>` and run
`BloodborneLauncher.exe --launch-options-smoke` from that test build.

## Graphics and effects

Choose output resolution, window resolution, reconstruction preset, FPS patch,
frame limiter, presentation mode and fullscreen independently. FG runtime
selection supports Standard Windows, experimental SM86 and official Streamline.
SM86 uses the managed private deployment prepared by prepare_dlssg_game.py.

FG on/off and 2X-6X factor use the same persistent fields as Insert. The runtime
enforces its actual capability limit in game. This host's 3X-6X functionality
was reported working by the user; the automated FPS comparison still covers 2X.
Generated presentations remain separate from real game frames.

NVIDIA Reflex Off / On / On + Boost shares the `reflex` INI setting with Insert
and works independently of FG in a prepared runtime. FG applies at least On
while retaining a saved Off preference. See [Reflex implementation and checks](WINDOWS_REFLEX.md).

FG currently needs DLSS/FSR4 inputs, Immediate presentation and a window at least
720 pixels high. The launcher selects Immediate when choosing an FG runtime.
Game effects and startup resolution patches are prepared on each launch;
in-game Apply and Restart remains supported by the existing runner.

Game Effects → **Vertex explosion fix → Disable FaceGen** is an opt-in checkbox,
also available in Insert. It saves `vertex_explosion_fix=1` in `bbport.ini` and
applies Kyo's [Disable Facegen patch](https://github.com/shadps4-emu/ps4_cheats/blob/28583f682b2341b519e79ac22b471a448a28869f/PATCHES/Bloodborne.xml)
for v1.09 at the next launch/restart. Uncheck and restart to restore FaceGen.
Custom face shapes for the player and affected NPCs are disabled while it is on;
this workaround covers FaceGen-related explosions, not every source of vertex
corruption. Original game assets and saves are not edited, and readback mode is
unchanged. The Linux 0.5 port uses Relaxed GPU readbacks for this issue; its source
does not contain this checkbox or the Disable Facegen patch.

2026-10-10 checks: 19 Rust model tests, 21 patch tests, 13 Python launcher tests,
and the C++ settings save/reload test passed. Native launcher checkbox clicks
and persistence were verified at 1180×820 and 1000×680. The two patch writes match
upstream and the original instructions in the local v1.09 ELF; a CLI off/on/off
cycle reproduced identical disabled patch binaries. Evidence is under
`out/windows-facegen-20261010`. Gameplay reproduction of a vertex explosion was
not performed for this option.

## Keyboard and mouse

The controller illustration is drawn by the launcher and has no external image
dependency. Click a controller button to locate its row. All 17 runtime actions
have primary and secondary assignments, including the four D-pad directions,
Options and both touchpad clicks. Bindings/default descriptions are read from
the runtime's shared header at compile time to avoid maintaining a second mapping.

Choose an input by name or Capture a keyboard/mouse button. A newly assigned
input is moved away from its previous action. Insert, F8 and F11 stay reserved.
Escape cancels Capture; choose Escape from the list to bind it. Mouse sensitivity,
capture, invert Y, keyboard/mouse enable and D-pad camera settings share the ini.
Movement remains WASD and camera remains mouse/arrow keys.

## Persistence and sessions

Launcher folder/runtime/window preferences live in:

    out/windows-launcher/settings.json

Graphics, effects, FG and bindings live in the active output folder:

    <output>/bbport.ini

Save merges edited keys with the current file and preserves unrelated keys.
Conflicting edits made outside the launcher require Reload. While a session
runs, launch/settings controls are disabled. Once the runner exits, the launcher
reloads Insert-written values. Startup arguments do not repeatedly override
upscaler/preset/output/effects/FPS-counter values.

Session Log combines preparation and native game output. Files are retained in:

    <output>/logs/launcher-<timestamp>.log
    <output>/logs/game-<timestamp>-<attempt>-stdout.log
    <output>/logs/game-<timestamp>-<attempt>-stderr.log

Each overlay restart gets a new attempt number, preserving earlier logs.
The GUI runner waits for the actual native process exit and redirects its
streams directly to files. This avoids a Windows PowerShell stream wait seen
with a hidden launcher session. Interactive command-line launches keep their
existing console stream behavior.

Save and return to the title screen in game before using Stop session. Stop
uses the game's existing stop-file watchdog, which exits the process without
performing an additional game save. Closing the launcher
while running offers to leave the game/runner active; the log continues on disk.
Launcher startup/callback errors are recorded under out/windows-launcher.

## Earlier Tk validation

    python tests/test_windows_launcher.py -v
    python launcher/windows_launcher.py --self-test
    python tools/windows/test_launcher_game.py --output out/launcher-test-new

The model regressions check concurrent ini edits, literal paths containing
spaces/apostrophes/Unicode, FG constraints, reserved keys, binding moves,
update metadata and save backup/source preservation. The Tk self-test constructs
all pages and the 34 binding controls. The optional real-game test uses a copied
save, scripted pad navigation, FG off and the launcher's own start/stop/log/reload
methods. Source saves and the original ini are checked afterward.

Initial Tk validation on 2026-10-08: all 13 model cases and the Tk self-test passed.
The full existing Windows CTest suite passed 92/92 with the two launcher tests.
The copied-save lifecycle test in out/windows-launcher-game-smoke-03 reached
gameplay, stopped the runner and reloaded an ini change; source save hashes and
the original ini remained unchanged. FG was disabled for this lifecycle check.
The windows-launcher-runner CTest separately exercises Unicode/literal argv,
both log streams, overlay restart exit 75 and a final nonzero game exit.

## Rust source and validation

launcher/rust/src/model.rs owns settings, SFO metadata, shared runtime bindings,
path checks and save import. session.rs owns the child process, log streaming
and stop-file lifecycle. ui.rs owns persistent component entities and GPUI
subscriptions. Blocking file dialogs and save imports run outside the UI thread.
Binding capture is sampled only while this window owns focus; assignments use
SDL physical IDs. Choose numpad Enter explicitly from the dropdown if needed.
Graphics, effects and input controls are disabled during an active game session.

    cargo test --manifest-path launcher/rust/Cargo.toml --no-default-features --test model
    cargo clippy --manifest-path launcher/rust/Cargo.toml --all-targets -- -D warnings
    cargo fmt --manifest-path launcher/rust/Cargo.toml --check

The optional ui-validation feature provides app-owned smoke entry points. It is
excluded from the shipped build. --ui-smoke renders all pages and saves native
GPUI frame snapshots under out/windows-launcher/rust-ui-smoke. --game-smoke is
driven by tools/windows/test_launcher_rust_game.py with a copied save and FG off.
It uses the actual UI launch/stop/reload methods, checks the original save/ini,
and restores launcher preferences after the test.

The first Rust game smoke passed in out/windows-rust-launcher-game-smoke-01:
gameplay reached, stop exit 0, changed ini reloaded, source save and original
ini unchanged. This is a lifecycle check, not an FG performance comparison.
All 11 Rust model/input regressions passed, along with warnings-as-errors Clippy
and formatting checks. Native GPUI frame snapshots cover all five pages, the FG
and binding sections, and the 1000x680 minimum window size.

The 2026-10-09 visual refresh adds supplied Bloodborne artwork, a warm
gold component palette, an expanded workshop sidebar, section icons and a home
summary of output resolution, reconstruction and frame generation. The summary
reads the current profile and its Edit button opens Graphics. The home launch
button uses the same validated launch method as the persistent bottom toolbar.
Stop session appears while a session is active. All five settings/log pages
remain available. The supplied cover replaces the sidebar moon emblem, and the
seated hunter replaces the city banner. Artwork is compiled into the executable
from launcher/rust/assets/brand-cover.jpg and hunter-workshop.png; it needs no
download or adjacent asset folder. The original AVIF is decoded to a lossless
PNG for native image loading. The banner scales without stretching, anchors
the image at the top to preserve the hunter's head, and adds a native gradient
behind the text. Image decoding starts asynchronously when the launcher opens.
Game version metadata is cached between folder changes/profile reloads, rather
than read from disk on every UI redraw. The theme uses Theme::update to keep
component colors, background tokens and the Base layer in sync.
UI snapshots additionally cover the home and graphics pages at 1000x680.

The controller panel embeds the supplied diagram unchanged from
launcher/rust/assets/controller.png. The controller-dark.svg presentation layer
maps its colors to the dark theme while preserving image alpha and geometry;
the panel has no light backing. Interactive targets use coordinates relative to the
original 1672x941 image, so the 17 supported binding actions stay aligned when
the panel resizes. Targets use transparent button styles without floating
tooltip labels or gray hover boxes. A gold contour follows the selected
button's actual shape. Touchpad left/right are separate targets. The printed Share
and PS/Home labels belong to the supplied illustration and do not add runtime
binding actions.

Selection contours and hit targets are defined in launcher/rust/src/controller.rs.
The launcher starts with no control selected. The diagram has no selection
contour until the user clicks a control or binding action.
The transparent buttons handle interaction and accessibility; their rectangular
click areas never draw selection borders. Small generated SVGs share the image's
viewBox and use measured face-button circles, analog circles, D-pad outlines,
touchpad halves and the Options pill. Cross is centered at (1201,491), Square at
(1115,405), and the analog contours at (644,561) and (1027,561).
Selecting a shoulder button highlights its physical cap, printed label and
connecting line together. The printed L1/L2/R1/R2 labels remain additional,
larger click targets for the same actions. They do not create separate mappings.
Right-side badge rectangles use the measured R1/R2 text bounds instead of
mirroring the left rectangles, keeping padding balanced around the wider glyphs.

The native UI smoke dispatches real mouse events at the center and four points
near the edges of each rendered target, then checks the chosen binding. Its
330-click check covers all 17 diagram controls, the four shoulder labels and
returning to Cross, at 1180, 1000 and 1440 pixel window widths. It also verifies
rendered target positions/sizes against source geometry with pixel-rounding
tolerance, and saves selected-state snapshots for every control at each width.
Results are in rust-ui-smoke/controller-clicks.json and controller-geometry-*.json.

The launcher motion refresh uses native GPUI transitions and springs. Navigation
has a sliding gold indicator and interpolated text colors, while page content
enters with a short fade and an eight-pixel rise. Cards have icon headers,
section dividers, staggered entrances and warm border/background hover feedback.
Primary, outline, ghost and stop buttons interpolate their colors and give
subtle press feedback. Controller selection contours fade in without changing
their hit areas. Native Select popovers and Switch thumb springs use the common
motion theme. Hover transitions take 150 ms, page/card entrances 260 ms, and
card delays range from 0 to 50 ms. Interrupted navigation springs retarget the
latest selection. These effects respect GPUI's reduced-motion setting and
request animation frames only while a transition is running.

The Windows MSVC executable reserves an 8 MiB stack for deep native render
traversal; Windows commits pages as needed. This fixes a debug-build callback
crash on entering the full bindings page with animated components.

The optional --motion-smoke entry point tests 12 real navigation clicks,
intermediate page frames, rapid transition interruption, native Select opening
and Escape dismissal, a reversible Switch toggle, primary-button hover color
interpolation, releasing a pressed button outside its bounds, and reduced motion.
It also checks that two settled native frames are identical. It does not launch
the game or save settings. Results and frames are stored with the UI snapshots
in rust-ui-smoke/motion-checks.json and motion-*.png. The full 330-click controller
test and all 11 model regressions pass with the refreshed layout.

Graphics > Display > Window mode offers Windowed, Borderless Fullscreen and
Fullscreen. This is shared with Insert > Window Mode using the window_mode key
in bbport.ini (windowed, borderless or fullscreen). The launcher no longer sends
the old fullscreen boolean on every launch, so changes from Insert/F11 survive
relaunching. Old fullscreen=true launcher preferences migrate to borderless
only when the ini has no window_mode yet; explicit ini choices take precedence.
The earlier Python UI also uses the same three-choice setting.

The game applies mode changes live on its SDL window thread. Borderless uses
the window's monitor at the desktop resolution. Fullscreen chooses a supported
exclusive display mode nearest the startup window resolution, with the desktop
refresh rate as the preference. It leaves the game's output/scene resolution
settings unchanged. Returning to Windowed restores the previous window size and
position. Failed changes restore the preceding mode and show an Insert warning.
F11 toggles Windowed and the last successfully selected fullscreen kind in that
session (Fullscreen initially). Key repeat/key-up never trigger another toggle.
F11 is reserved alongside Insert and F8 and cannot become a controller binding.
Successful live mode changes are saved immediately; menu and window-thread ini
writes are serialized.

An explicit runner override is available as -WindowMode windowed|borderless|fullscreen.
Otherwise the ini governs startup; BB_WINDOW_MODE overrides it for diagnostics.
Legacy BB_FULLSCREEN remains supported, with BB_WINDOW_MODE taking precedence.
The runner clears its explicit mode override on an overlay-requested restart.
The Windows gpu-window-mode CTest exercises actual SDL mode/flag transitions,
window restoration, F11 routing/repeat handling and ini reload/environment
precedence. Native launcher smoke checks choose all three options through the
real Select control. The copied-save game smoke accepts --window-mode and an
isolated --runtime for comparisons; its default native/startup profile makes
window tests independent from the user's reconstruction/live-resolution preset.

Window-mode validation passed: 12 Rust model tests, warnings-as-errors Clippy,
13 Python launcher tests, the input/capture tests, actual SDL window transitions,
and native launcher selection of all three choices. The copied-save Borderless
game run reached gameplay, exited normally and reloaded settings; original save
and ini files were unchanged. Its report is
out/windows-window-mode-review/game-borderless-final/summary.json. This run used
FG off, preset 1 and live resolution enabled. An earlier startup attempt hit a
GPU PM4 assertion; the repeat with those same settings completed successfully.
Fullscreen/F11 transitions were checked with SDL, but not during gameplay with
FG enabled.

The puzzle-icon Mods page supports adding extracted loose-file mods, per-mod and
global switches, priority ordering, refresh and removal from the library. Choices
save automatically and apply on the next session. The Windows runner mounts mods
over the official update without changing original files. See
[Windows mods](WINDOWS_MODS.md) for layouts, command-line profiles and validation.
