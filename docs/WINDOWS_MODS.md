# Windows launcher mods

Open **Mods** (the puzzle icon) and choose **Add mod**. Select the extracted
folder of one mod. Archives must be extracted first; files stay in the selected
folder. Each entry has an **On/Off** switch, **Move up/down**, **Open folder** and
**Remove**. **Enable mods** switches the entire library off without losing each
mod's selection. **Refresh** updates file counts after editing a mod folder.

Changes save automatically to `out/windows-launcher/settings.json` and apply on
the next launch. Mod editing is disabled during a running session. Removing an
entry only removes it from the list, keeping its files. Lower entries take
priority when enabled mods replace the same path; overlaps appear in Session Log.

These layouts are detected automatically, including case variations:

```text
My mod/dvdroot_ps4/chr/...
My mod/chr/...                 # no dvdroot_ps4 wrapper required
My mod/map/...
My mod/param/...
My mod/app0/dvdroot_ps4/...
My mod/CUSA03173/dvdroot_ps4/...
My mod/Extracted version 2/dvdroot_ps4/...
```

You can also select `dvdroot_ps4` itself or an individual game asset folder such
as `chr`. Direct asset folders include `action`, `chr`, `event`, `facegen`, `font`,
`map`, `menu`, `movie`, `msg`, `mtd`, `obj`, `other`, `param`, `paramdef`, `parts`,
`remo`, `script`, `sfx`, `shader` and `sound`. When selecting a complete game-shaped
folder, only its `dvdroot_ps4` assets are loaded. Modded `eboot.bin`, `sce_module`
and `sce_sys` are outside this loader's scope; executable patches use the existing
patch system. A mod must match the game version it was made for.

The runner applies official updates first, then enabled mods. It builds a separate
read-only game mount under the selected output's `mod-mounts/`; originals and saves
stay in place. Windows uses hard links and ordinary directories, so admin access
and Developer Mode are not required. The output must be on the game's drive;
individual mods can be on another drive, with a copy fallback for their files.
Mounts are cached across runs. Changing enabled files or priority produces a new
mount; switching everything off uses the original/officially updated game directly.
Symlinks, junctions, case collisions and file/directory conflicts are rejected.

For command-line use, create a JSON profile with absolute folder paths:

```json
{
  "enabled": true,
  "mods": [
    {"name": "Hunter outfit", "path": "C:\\Mods\\Hunter outfit", "enabled": true},
    {"name": "Lighting", "path": "D:\\Mods\\Lighting", "enabled": false}
  ]
}
```

Pass `-ModsConfig C:\Mods\selection.json` to `run-windows.ps1`. The native launcher
creates a per-session snapshot beside the session logs, preserving the selected
order for the whole run and overlay restarts.

Validation covers layout detection, toggles, priority, update-before-mod layering,
case normalization, cache reuse/invalidation, cross-drive copy fallback and source
preservation. A synthetic guest calls the actual PS4 open/read/close imports through
both `/app0` and `/hostapp`, checks mod-off fallback and verifies write rejection.
Native `--mods-smoke` clicks the switches, reorders and removes entries, verifies
persisted settings and captures layouts at 1000 and 1440 pixel widths. Its report
is `out/windows-mod-review/native/summary.json`. A normal runner PrepareOnly test
against the local 1.09 dump is recorded in `out/windows-mod-review/runner/`.
Community-mod compatibility/gameplay must be checked with the particular mod.
