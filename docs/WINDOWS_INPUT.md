# Keyboard and mouse controls

Keyboard and mouse input is enabled by default in the GPU build and works with
a gamepad connected. The port translates these inputs into the game's PS4 pad
state. Run the rebuilt `out/windows/bb-game.exe` through the normal runner:

```powershell
.\run-windows.ps1 -Gpu -SkipBuild -UserDirectory out/windows-play-user -Timeout 0
```

The previous graphics settings are retained. No additional input flag is needed.

| Action | Default input | PS4 input |
| --- | --- | --- |
| Move | W A S D | Left stick |
| Walk | Hold Left Ctrl while moving | Half-strength left stick |
| Camera | Mouse movement; unassigned arrow keys also work | Right stick |
| Attack | Left mouse button or 3 | R1 |
| Heavy attack | Mouse side button 1 (X1) or F | R2 |
| Firearm / left-hand attack | Right mouse button or R | L2 |
| Transform weapon | Mouse side button 2 (X2) or 1 | L1 |
| Lock on | Middle mouse button or C | R3 |
| Dodge / sprint / menu back | Left Shift | Circle |
| Menu back | Escape | Circle |
| Interact / menu confirm | Space | Cross |
| Use quick item | E | Square |
| Heal | Q | Triangle |
| D-pad | I (up), J (left), K (down), L (right) | D-pad |
| Game menu | Enter | Options |
| Left-stick click | Z | L3 |
| Left / right touchpad click | Tab / Backspace | Touchpad + touch position |
| Port settings | Insert | Host overlay |
| Release / capture cursor | F8 | Host shortcut |
| Toggle fullscreen | F11 | Host shortcut |

Mouse side-button numbers are SDL's X1/X2; the button locations depend on the
mouse. The game's own menus use the same pad mapping: I/K navigate and Space
confirms. They retain their controller button prompts.

## Capture and settings

The focused game window captures and hides the cursor. **F8** temporarily
releases it; press F8 again in the focused game window to resume mouse camera
and attacks. Keyboard controls remain available while the cursor is released.
Opening the port overlay, switching to another application, system text entry,
or closing the window releases capture and clears held gameplay input. After
returning, press previously held keys/buttons again. Alt+Tab is not mapped to
the game's touchpad.

In **Insert → Keyboard & Mouse (Live)**, change enable/capture, sensitivity
(0.10–5.00, default 1.00), or invert Y. These settings apply without a restart
and are saved when the overlay closes, in the same `bbport.ini` as graphics
settings. For the default Windows command this is
`out/windows-game/bbport.ini`; `-OutputDirectory` or `BB_CONFIG` changes its
location. `-UserDirectory` selects saves/cache, not this settings file.

```ini
keyboard_mouse=1
mouse_capture=1
mouse_sensitivity=1.00
mouse_invert_y=0
dpad_camera=0
```

The corresponding startup environment overrides are `BB_KBM`,
`BB_MOUSE_CAPTURE`, `BB_MOUSE_SENSITIVITY`, `BB_MOUSE_INVERT_Y`, and
`BB_DPAD_CAMERA`. The
temporary F8 state is not saved. Disabling keyboard/mouse leaves gamepad input
available. Disabling mouse capture leaves keyboard and arrow-key camera input
available, while gameplay mouse movement/clicks are ignored.

**D-pad Controls Camera** in the same overlay section is off by default. Enable
it to use D-pad directions as the right stick. It applies to the physical
controller D-pad and its configured keyboard/mouse bindings, including with
KBM disabled for physical controllers. While enabled, D-pad directions control
the camera instead of selecting items or navigating game menus; disable it to
restore those actions. The setting applies without restarting and is saved
on closing Settings (`dpad_camera=0|1`). Held D-pad input overrides mouse/right
stick camera input; releasing it restores the other input source.
Arrow keys assigned to D-pad directions respect this toggle. Unassigned arrow
keys retain their camera shortcut; arrows assigned to a button perform that
button's action instead of also moving the camera.

## Edit key bindings

Open **Insert → Keyboard & Mouse (Live) → Key Bindings**. The table includes
Cross, Triangle, Square, Circle, L1/L2/L3, R1/R2/R3 and all four D-pad directions,
plus Options and the left/right touchpad clicks. Each row has **Primary** and
**Secondary** slots; either slot accepts a keyboard key or a mouse button.

Click a slot and press the desired keyboard key. For a mouse assignment, select
Mouse Left, Mouse Right, Mouse Middle, Mouse Side 1 or Mouse Side 2 in the
assignment dialog. **Unbound** removes that slot, **Default** restores its
default, and **Cancel** or Escape cancels the edit. To assign Escape itself,
click the dialog's **Escape** button. Insert closes the overlay, F8 retains
its cursor shortcut, and F11 toggles fullscreen. These keys cannot be assigned
to a game button. Switching
to another application cancels a pending assignment.

**F11** toggles between Windowed and the last fullscreen mode selected during
the session (Fullscreen initially). Choose Windowed, Borderless Fullscreen or
Fullscreen in **Insert > Window Mode** or the launcher's **Graphics > Display**.
Live changes are saved immediately to the shared `bbport.ini`.

Assigning an already used key/button moves it from its previous slot to the
new control. **Reset All Bindings to Defaults** restores the complete table.
Changes take effect without restarting and are saved when Settings closes.
WASD and Left Ctrl retain their movement/walk roles even if assigned to a
button. Unassigned arrow keys control the camera. Physical gamepad buttons
retain their PS4 mappings except for the optional D-pad camera mode.

Bindings use `bind_<action>` and `bind_<action>_alt` keys in `bbport.ini`, with
positive SDL scancodes, negative SDL mouse button numbers, and 0 for Unbound.
For example, Cross on G with left mouse as a second input is:

```ini
bind_cross=10
bind_cross_alt=-1
```

Keyboard names shown in the menu are display labels; stored scancode IDs avoid
ambiguity between keys with the same label. Action identifiers and defaults
are shared between the overlay, settings and runtime in
`src/runtime_pad_bindings.h`, ready for use by the future launcher.

## Implementation and validation

The window thread handles SDL3 events and owns relative mouse capture. It
publishes mouse velocity in approximately 8 ms buckets, normalized by elapsed
monotonic time. Guest pad reads use a mutex-protected snapshot; reading it
repeatedly does not consume or re-apply a relative-motion delta. Mouse vectors
are capped radially and compensated for the game's stick dead zone. An idle
bucket returns the camera to neutral, and a 32 ms expiry prevents stale input
if the window event pump stalls. WASD diagonals have the same stick magnitude
as cardinal movement, and opposing keys cancel.

Mouse camera uses the original analog-stick camera speed limit. Its feel needs
an interactive check with the player's mouse/DPI; sensitivity does not remove
that game-side limit.

`pad-virtual-controller` exercises keyboard input with an attached controller,
movement cancellation/diagonals/walking, mouse buttons and triggers, camera
publication/expiry, repeated reads, invert Y, capture release and blocked-input
reset. `gpu-input-settings` checks settings persistence and invalid sensitivity
values. Physical mouse feel and actual desktop focus transitions require an
interactive check in the game.
The binding tests additionally check every PS4 action after remapping, two
input sources holding one action, removal of the old binding, an all-unbound
profile, preserved physical gamepad buttons, persisted secondary inputs,
invalid/reserved codes and resets. `gpu-keybind-capture` checks capture,
repeat suppression, cancellation and host shortcuts.

Validation on 2026-10-06: the complete Windows GPU build and **88/88 CTest
checks passed** (`out/kbm-build.log`). A separate save/cache copy reached the
existing gameplay scene at 720p output and scene resolution, DLSS Native AA,
Mailbox and the 120 FPS host limit. Two 20-second stationary passes measured
70.72 / 70.84 FPS (p99 17.00 / 14.87 ms), within the previous scene's measured
70–72 FPS range. Neither pass compiled new shaders. This is a launch and
performance check using `BB_PAD_SCRIPT_ONLY=1`, not a physical KBM playtest.
Source save and cache hashes were unchanged and the test exited cleanly.
Evidence: `out/windows-input-01/kbm-counter-static-startup-01/summary.json`,
`verification.json`, `session.json`, and `game.log`.

Editable-binding follow-up on 2026-10-06: the updated complete Windows build
passed **89/89 CTest checks**, including the stronger remapping tests and the
new capture-state test (`out/keybind-build.log`). The gameplay/performance check
above predates the editable-binding menu; clicking through that menu in the
actual game has not yet been manually verified.

The D-pad camera follow-up also passed the complete Windows build and 89/89
tests (`out/dpad-camera-build.log`). Coverage includes live toggle-on/off,
normal D-pad button restoration, keyboard diagonals/opposing directions,
custom key/mouse bindings, assigned arrows without the old camera shortcut,
physical D-pad use with KBM disabled, right-stick restoration, focus/overlay
blocking and settings persistence. Actual in-game interaction remains a
manual verification step.
