// SPDX-License-Identifier: GPL-2.0-or-later
use anyhow::{Context, Result, bail, ensure};
use chrono::Local;
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    io::Write,
    path::{Component, Path, PathBuf},
};

pub type Values = BTreeMap<String, String>;
pub const RESOLUTIONS: &[&str] = &["1280x720", "1920x1080", "2560x1440", "3840x2160"];
pub const WINDOW_MODES: &[&str] = &["windowed", "borderless", "fullscreen"];
pub const EFFECTS: &[(&str, &str, bool)] = &[
    ("effect_chromatic_aberration", "Chromatic aberration", true),
    ("effect_dof", "Depth of field", true),
    ("effect_motion_blur", "Motion blur", true),
    ("effect_ssao", "SSAO", true),
    ("effect_game_aa", "In-game antialiasing", true),
    ("effect_dynamic_shadows", "Dynamic light shadows", true),
    ("effect_ssr", "Screen-space reflections", false),
    ("vertex_explosion_fix", "Vertex explosion fix", false),
    ("skip_intro", "Skip intro videos", false),
    ("debug_camera", "Free camera (Cross + L3)", false),
    ("debug_menu", "Debug menu (requires font files)", false),
];
#[derive(Clone, Debug)]
pub struct Binding {
    pub key: String,
    pub label: String,
    pub action: String,
    pub defaults: [i32; 2],
}

pub fn choices() -> Vec<(String, i32)> {
    let mut v = vec![("Unbound".into(), 0)];
    v.extend((0..26).map(|i| (((b'A' + i as u8) as char).to_string(), i + 4)));
    v.extend((1..10).map(|i| (i.to_string(), i + 29)));
    v.extend(
        [
            ("0", 39),
            ("Enter", 40),
            ("Escape", 41),
            ("Backspace", 42),
            ("Tab", 43),
            ("Space", 44),
            ("-", 45),
            ("=", 46),
            ("[", 47),
            ("]", 48),
            ("\\", 49),
            (";", 51),
            ("'", 52),
            ("Grave", 53),
            (",", 54),
            (".", 55),
            ("/", 56),
            ("Caps Lock", 57),
            ("Print Screen", 70),
            ("Scroll Lock", 71),
            ("Pause", 72),
            ("Home", 74),
            ("Page Up", 75),
            ("Delete", 76),
            ("End", 77),
            ("Page Down", 78),
            ("Right", 79),
            ("Left", 80),
            ("Down", 81),
            ("Up", 82),
            ("Num Lock", 83),
            ("Numpad /", 84),
            ("Numpad *", 85),
            ("Numpad -", 86),
            ("Numpad +", 87),
            ("Numpad Enter", 88),
            ("Numpad 0", 98),
            ("Numpad .", 99),
            ("Intl Backslash", 100),
            ("Menu", 101),
            ("Left Ctrl", 224),
            ("Left Shift", 225),
            ("Left Alt", 226),
            ("Left Win", 227),
            ("Right Ctrl", 228),
            ("Right Shift", 229),
            ("Right Alt", 230),
            ("Right Win", 231),
            ("Mouse Left", -1),
            ("Mouse Middle", -2),
            ("Mouse Right", -3),
            ("Mouse Side 1", -4),
            ("Mouse Side 2", -5),
        ]
        .map(|(k, c)| (k.into(), c)),
    );
    v.extend(
        (1..=12)
            .filter(|&i| i != 8 && i != 11)
            .map(|i| (format!("F{i}"), 57 + i)),
    );
    v.extend((1..=9).map(|i| (format!("Numpad {i}"), 88 + i)));
    v
}
fn symbol(s: &str) -> i32 {
    let name = s.strip_prefix("SDL_SCANCODE_").unwrap_or(s);
    let name = match name {
        "SPACE" => "Space",
        "LSHIFT" => "Left Shift",
        "ESCAPE" => "Escape",
        "RETURN" => "Enter",
        "TAB" => "Tab",
        "BACKSPACE" => "Backspace",
        "-SDL_BUTTON_X2" => "Mouse Side 2",
        "-SDL_BUTTON_X1" => "Mouse Side 1",
        "-SDL_BUTTON_RIGHT" => "Mouse Right",
        "-SDL_BUTTON_LEFT" => "Mouse Left",
        "-SDL_BUTTON_MIDDLE" => "Mouse Middle",
        "0" => "Unbound",
        s => s,
    };
    choices()
        .into_iter()
        .find(|(n, _)| n == name)
        .unwrap_or_else(|| panic!("Unknown runtime binding: {s}"))
        .1
}
pub fn bindings() -> Vec<Binding> {
    let re =
        Regex::new(r#"X\(\w+, "([^"]+)", "([^"]+)", "([^"]+)", 0x[0-9a-f]+, ([\w-]+), ([\w-]+)\)"#)
            .unwrap();
    let v: Vec<_> = re
        .captures_iter(include_str!("../../../src/runtime_pad_bindings.h"))
        .map(|c| Binding {
            key: c[1].into(),
            label: c[2].into(),
            action: c[3].into(),
            defaults: [symbol(&c[4]), symbol(&c[5])],
        })
        .collect();
    assert_eq!(v.len(), 17, "Runtime controller metadata changed");
    v
}
pub fn defaults() -> Values {
    let mut v: Values = [
        ("upscaler", "off"),
        ("preset", "0"),
        ("output_res", "1920x1080"),
        ("live_resolution", "0"),
        ("window_mode", "windowed"),
        ("sharpen", "1"),
        ("sharpness", "0.30"),
        ("jitter", "1"),
        ("object_motion", "1"),
        ("show_fps", "1"),
        ("frame_generation", "0"),
        ("fg_multiplier", "2"),
        ("reflex", "on"),
        ("keyboard_mouse", "1"),
        ("mouse_capture", "1"),
        ("mouse_invert_y", "0"),
        ("mouse_sensitivity", "1.00"),
        ("dpad_camera", "0"),
        ("model_lod", "0"),
    ]
    .into_iter()
    .map(|(k, v)| (k.into(), v.into()))
    .collect();
    for &(k, _, enabled) in EFFECTS {
        v.insert(k.into(), if enabled { "1" } else { "0" }.into());
    }
    for b in bindings() {
        for (i, suffix) in ["", "_alt"].iter().enumerate() {
            v.insert(format!("bind_{}{suffix}", b.key), b.defaults[i].to_string());
        }
    }
    v
}
pub fn read_ini(path: &Path) -> Result<Values> {
    if !path.exists() {
        return Ok(Values::new());
    }
    let text = fs::read_to_string(path).with_context(|| format!("Read {}", path.display()))?;
    Ok(text
        .trim_start_matches('\u{feff}')
        .lines()
        .filter_map(|l| {
            let l = l.trim();
            if l.starts_with(['#', ';']) {
                return None;
            }
            l.split_once('=')
                .map(|(k, v)| (k.trim().into(), v.trim().into()))
        })
        .collect())
}
pub fn atomic_write(path: &Path, text: &str) -> Result<()> {
    let parent = path.parent().context("Missing parent folder")?;
    fs::create_dir_all(parent)?;
    let mut temp = tempfile::NamedTempFile::new_in(parent)?;
    temp.write_all(text.as_bytes())?;
    temp.as_file().sync_all()?;
    temp.persist(path).map_err(|e| e.error)?;
    Ok(())
}
#[derive(Clone)]
pub struct Profile {
    pub path: PathBuf,
    pub values: Values,
    pub baseline: Values,
    pub loaded: Values,
}
impl Profile {
    pub fn load(path: PathBuf) -> Result<Self> {
        let baseline = read_ini(&path)?;
        let mut values = defaults();
        for (k, v) in &baseline {
            if values.contains_key(k) {
                values.insert(k.clone(), v.clone());
            }
        }
        Ok(Self {
            path,
            loaded: values.clone(),
            values,
            baseline,
        })
    }
    pub fn dirty(&self) -> bool {
        self.values != self.loaded
    }
    pub fn migrate_fullscreen(&mut self, legacy: bool) {
        if legacy && !self.baseline.contains_key("window_mode") {
            self.set("window_mode", "borderless");
        }
    }
    pub fn save(&mut self) -> Result<()> {
        let current = read_ini(&self.path)?;
        let updates: Values = self
            .values
            .iter()
            .filter(|(k, v)| !current.contains_key(*k) || self.loaded.get(*k) != Some(*v))
            .map(|(k, v)| (k.clone(), v.clone()))
            .collect();
        for (k, v) in &updates {
            ensure!(
                current.get(k) == self.baseline.get(k)
                    || !current.contains_key(k)
                    || current.get(k) == Some(v),
                "{k} changed outside the launcher. Reload before saving."
            );
        }
        let text = if self.path.exists() {
            fs::read_to_string(&self.path)?
        } else {
            String::new()
        };
        let mut seen = BTreeSet::new();
        let mut lines = Vec::new();
        for line in text.trim_start_matches('\u{feff}').lines() {
            let key = line.split_once('=').map(|(k, _)| k.trim()).unwrap_or("");
            if let Some(value) = updates.get(key) {
                if seen.insert(key.to_owned()) {
                    lines.push(format!("{key}={value}"));
                }
            } else {
                lines.push(line.to_owned());
            }
        }
        for (k, v) in &updates {
            if !seen.contains(k) {
                lines.push(format!("{k}={v}"));
            }
        }
        atomic_write(&self.path, &(lines.join("\n") + "\n"))?;
        *self = Self::load(self.path.clone())?;
        Ok(())
    }
    pub fn set(&mut self, key: &str, value: impl ToString) {
        self.values.insert(key.into(), value.to_string());
    }
    pub fn get(&self, key: &str) -> &str {
        self.values.get(key).map(String::as_str).unwrap_or("")
    }
    pub fn flag(&self, key: &str) -> bool {
        matches!(self.get(key), "1" | "on")
    }
    pub fn rebind(&mut self, key: &str, code: i32) -> Result<()> {
        ensure!(
            (-5..512).contains(&code) && ![65, 68, 73].contains(&code),
            "Insert, F8 and F11 are reserved."
        );
        ensure!(
            defaults().contains_key(key) && key.starts_with("bind_"),
            "Unknown binding"
        );
        if code != 0 {
            for (k, v) in &mut self.values {
                if k.starts_with("bind_") && k != key && *v == code.to_string() {
                    *v = "0".into();
                }
            }
        }
        self.set(key, code);
        Ok(())
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Preferences {
    pub mods_enabled: bool,
    pub mods: Vec<crate::mods::ModEntry>,
    pub game: String,
    pub patch: String,
    pub use_patch: bool,
    pub user: String,
    pub output: String,
    pub engine: String,
    pub build: String,
    pub window: String,
    pub present: String,
    pub fps_patch: String,
    pub fps_limit: u16,
    #[serde(default)]
    pub launch_options: String,
    // Read old launcher preferences only; new window modes live in bbport.ini.
    pub fullscreen: bool,
}
impl Preferences {
    pub fn load(root: &Path) -> Result<Self> {
        let local_path = root.join("out/windows-local.json");
        let local: serde_json::Value = if local_path.exists() {
            serde_json::from_str(fs::read_to_string(local_path)?.trim_start_matches('\u{feff}'))?
        } else {
            serde_json::json!({})
        };
        let fg = root
            .join("out/windows-fg-game/.bbport-dlssg-game")
            .is_file();
        let profile = if root.join("out/windows-fg-play/bbport.ini").is_file() {
            "out/windows-fg-play"
        } else {
            "out/windows-game"
        };
        let path = |p: &str| root.join(p).to_string_lossy().into_owned();
        let patch = local["patchDirectory"]
            .as_str()
            .filter(|s| Path::new(s).join("sce_sys/param.sfo").is_file())
            .unwrap_or("");
        let v = Self {
            mods_enabled: true,
            mods: Vec::new(),
            game: local["gameDirectory"].as_str().unwrap_or("").into(),
            patch: patch.into(),
            use_patch: !patch.is_empty(),
            user: path("out/windows-play-user"),
            output: path(profile),
            engine: if fg { "menu-sm86" } else { "off" }.into(),
            build: path(if fg {
                "out/windows-fg-game"
            } else {
                "out/windows"
            }),
            window: if fg { "1280x720" } else { "1920x1080" }.into(),
            present: if fg { "Immediate" } else { "Mailbox" }.into(),
            fps_patch: "uncap".into(),
            fps_limit: 120,
            launch_options: String::new(),
            fullscreen: false,
        };
        let saved = root.join("out/windows-launcher/settings.json");
        if !saved.exists() {
            return Ok(v);
        }
        let mut json = serde_json::to_value(v)?;
        let disk: serde_json::Value = serde_json::from_str(&fs::read_to_string(saved)?)?;
        if let Some(map) = disk.as_object() {
            for (k, v) in map {
                if json.get(k).is_some() {
                    json[k] = v.clone();
                }
            }
        }
        serde_json::from_value(json).context("Launcher preferences contain invalid values")
    }
    pub fn save(&self, root: &Path) -> Result<()> {
        atomic_write(
            &root.join("out/windows-launcher/settings.json"),
            &(serde_json::to_string_pretty(self)? + "\n"),
        )
    }
    pub fn folder(&self, key: &str) -> &str {
        match key {
            "game" => &self.game,
            "patch" => &self.patch,
            "user" => &self.user,
            "output" => &self.output,
            "build" => &self.build,
            _ => "",
        }
    }
    pub fn set_folder(&mut self, key: &str, path: String) {
        match key {
            "game" => self.game = path,
            "patch" => self.patch = path,
            "user" => self.user = path,
            "output" => self.output = path,
            "build" => self.build = path,
            _ => {}
        }
    }
}
fn unprefix(path: PathBuf) -> PathBuf {
    let p = path.to_string_lossy();
    if let Some(p) = p.strip_prefix(r"\\?\UNC\") {
        PathBuf::from(format!(r"\\{p}"))
    } else if let Some(p) = p.strip_prefix(r"\\?\") {
        PathBuf::from(p)
    } else {
        path
    }
}
pub fn resolve(root: &Path, value: &str) -> Result<PathBuf> {
    ensure!(!value.trim().is_empty(), "Select all required folders.");
    let path = Path::new(value);
    let path = if path.is_absolute() {
        path.to_path_buf()
    } else {
        root.join(path)
    };
    let mut clean = PathBuf::new();
    for c in path.components() {
        match c {
            Component::ParentDir => {
                clean.pop();
            }
            Component::CurDir => {}
            _ => clean.push(c.as_os_str()),
        }
    }
    let mut existing = clean.as_path();
    let mut suffix = Vec::new();
    while !existing.exists() {
        suffix.push(existing.file_name().context("Invalid folder")?.to_owned());
        existing = existing.parent().context("Invalid root")?;
    }
    let mut normalized = unprefix(fs::canonicalize(existing)?);
    for c in suffix.into_iter().rev() {
        normalized.push(c);
    }
    Ok(normalized)
}
fn within(a: &Path, b: &Path) -> bool {
    #[cfg(windows)]
    {
        PathBuf::from(a.to_string_lossy().to_lowercase())
            .starts_with(PathBuf::from(b.to_string_lossy().to_lowercase()))
    }
    #[cfg(not(windows))]
    {
        a.starts_with(b)
    }
}
pub fn metadata(game: &Path) -> Result<Values> {
    let b = fs::read(game.join("sce_sys/param.sfo"))
        .context("Select a game folder containing sce_sys/param.sfo")?;
    let u16_at = |i| -> Result<usize> {
        Ok(u16::from_le_bytes(b.get(i..i + 2).context("Invalid SFO")?.try_into()?) as usize)
    };
    let u32_at = |i| -> Result<usize> {
        Ok(u32::from_le_bytes(b.get(i..i + 4).context("Invalid SFO")?.try_into()?) as usize)
    };
    ensure!(b.get(..4) == Some(b"\0PSF"), "Invalid SFO signature");
    let key_table = u32_at(8)?;
    let data = u32_at(12)?;
    let count = u32_at(16)?;
    ensure!(
        count < 4096 && key_table <= b.len() && data <= b.len(),
        "Invalid SFO tables"
    );
    let mut result = Values::new();
    for n in 0..count {
        let e = 20 + n * 16;
        let key = key_table
            .checked_add(u16_at(e)?)
            .context("SFO offset overflow")?;
        let raw = b.get(key..).context("Invalid SFO key")?;
        let end = raw
            .iter()
            .position(|&v| v == 0)
            .context("Invalid SFO key")?;
        let key = std::str::from_utf8(&raw[..end])?;
        if u16_at(e + 2)? != 0x204 {
            continue;
        }
        let off = data
            .checked_add(u32_at(e + 12)?)
            .context("SFO offset overflow")?;
        let len = u32_at(e + 4)?;
        let end = off.checked_add(len).context("SFO size overflow")?;
        let value = b.get(off..end).context("Invalid SFO value")?;
        result.insert(
            key.into(),
            String::from_utf8(value.iter().copied().take_while(|&v| v != 0).collect())?,
        );
    }
    Ok(result)
}
pub fn validate(root: &Path, p: &Preferences, v: &Profile, launching: bool) -> Result<()> {
    crate::session::parse_launch_options(&p.launch_options)?;
    let game = resolve(root, &p.game)?;
    let output = resolve(root, &p.output)?;
    let user = resolve(root, &p.user)?;
    let build = resolve(root, &p.build)?;
    ensure!(
        !within(&output, &user),
        "Choose an output folder separate from the save root."
    );
    for dir in [&output, &user, &build] {
        ensure!(
            !within(dir, &game),
            "Output, saves and executables must stay outside the game dump."
        );
    }
    ensure!(
        RESOLUTIONS.contains(&v.get("output_res")) && RESOLUTIONS.contains(&p.window.as_str()),
        "Choose a supported resolution."
    );
    ensure!(
        ["off", "dlss", "fsr3", "fsr4", "fsr411", "taa"].contains(&v.get("upscaler")),
        "Unsupported upscaler"
    );
    ensure!(
        ["0", "1", "2", "3", "4"].contains(&v.get("preset")),
        "Unsupported reconstruction preset"
    );
    ensure!(
        ["0", "1", "auto"].contains(&v.get("live_resolution")),
        "Unsupported resolution mode"
    );
    ensure!(
        WINDOW_MODES.contains(&v.get("window_mode")),
        "Unsupported window mode"
    );
    ensure!(
        ["off", "menu", "menu-sm86"].contains(&p.engine.as_str()),
        "Unsupported FG runtime"
    );
    ensure!(
        ["Immediate", "Mailbox", "Fifo"].contains(&p.present.as_str()),
        "Unsupported present mode"
    );
    ensure!(
        ["uncap", "60", "90", "preserve"].contains(&p.fps_patch.as_str()) && p.fps_limit <= 480,
        "Invalid FPS setting"
    );
    ensure!(
        (2..=6).contains(&v.get("fg_multiplier").parse::<u8>()?),
        "FG multiplier must be 2X-6X"
    );
    ensure!(
        ["off", "on", "boost"].contains(&v.get("reflex")),
        "Invalid Reflex mode"
    );
    let sensitivity = v.get("mouse_sensitivity").parse::<f32>()?;
    let sharpness = v.get("sharpness").parse::<f32>()?;
    ensure!(
        sensitivity.is_finite()
            && (0.1..=5.).contains(&sensitivity)
            && sharpness.is_finite()
            && (0.0..=2.).contains(&sharpness),
        "Sensitivity: 0.1-5; sharpness: 0-2."
    );
    ensure!(
        (-2..=2).contains(&v.get("model_lod").parse::<i8>()?),
        "Invalid model detail"
    );
    for (k, c) in &v.values {
        if k.starts_with("bind_") {
            let c = c.parse::<i32>()?;
            ensure!(
                (-5..512).contains(&c) && ![65, 68, 73].contains(&c),
                "Invalid or reserved binding: {k}"
            );
        }
    }
    if v.flag("frame_generation") {
        ensure!(
            p.engine != "off",
            "Select an FG runtime before enabling Frame Generation."
        );
        ensure!(
            ["dlss", "fsr4", "fsr411"].contains(&v.get("upscaler")),
            "FG requires DLSS or FSR4 scene inputs."
        );
    }
    ensure!(
        p.engine == "off" || p.present == "Immediate",
        "This FG runtime requires Immediate presentation."
    );
    if p.use_patch {
        let patch = resolve(root, &p.patch)?;
        for dir in [&output, &user, &build] {
            ensure!(
                !within(dir, &patch),
                "Keep output, saves and executables outside the update dump."
            );
        }
        if launching {
            let m = metadata(&patch)?;
            ensure!(
                m.get("TITLE_ID") == metadata(&game)?.get("TITLE_ID")
                    && m.get("APP_VER").map(String::as_str) == Some("01.09"),
                "Update must match the game and be 01.09."
            );
        }
    }
    if !launching {
        return Ok(());
    }
    ensure!(
        game.join("eboot.bin").is_file(),
        "Game folder is missing eboot.bin"
    );
    let m = metadata(&game)?;
    ensure!(
        m.get("TITLE_ID").map(String::as_str) == Some("CUSA03173"),
        "This runner targets CUSA03173."
    );
    ensure!(
        p.use_patch || m.get("APP_VER").map(String::as_str) == Some("01.09"),
        "Select a merged 01.09 game or its separate update."
    );
    ensure!(
        build.join("bb-game.exe").is_file() && build.join("bb-probe.exe").is_file(),
        "Executable folder needs bb-game.exe and bb-probe.exe."
    );
    if p.engine != "off" {
        ensure!(
            build.join("sl.interposer.dll").is_file(),
            "Prepare the private FG runtime first."
        );
        if p.engine == "menu-sm86" {
            ensure!(
                build.join(".bbport-dlssg-game").is_file(),
                "SM86 requires its managed private runtime."
            );
        }
        if p.engine == "menu" {
            ensure!(
                !build.join("version.dll").is_file(),
                "This folder has the SM86 proxy; select SM86 or an official runtime."
            );
        }
    }
    if v.get("upscaler") == "dlss" {
        ensure!(
            build.join("nvngx_dlss.dll").is_file(),
            "Missing nvngx_dlss.dll"
        );
    }
    Ok(())
}
fn copy_tree(source: &Path, dest: &Path) -> Result<()> {
    let m = fs::symlink_metadata(source)?;
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        ensure!(
            m.file_attributes() & 0x400 == 0,
            "Save import does not follow linked files."
        );
    }
    ensure!(
        !m.file_type().is_symlink(),
        "Save import does not follow links."
    );
    if m.is_dir() {
        fs::create_dir(dest)?;
        for e in fs::read_dir(source)? {
            let e = e?;
            copy_tree(&e.path(), &dest.join(e.file_name()))?;
        }
    } else {
        ensure!(m.is_file(), "Unsupported save entry");
        fs::copy(source, dest)?;
    }
    Ok(())
}
pub fn import_save(source: &Path, user: &Path) -> Result<(PathBuf, Option<PathBuf>)> {
    let original = fs::symlink_metadata(source)?;
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        ensure!(
            original.file_attributes() & 0x400 == 0,
            "Save import does not follow linked folders."
        );
    }
    ensure!(
        !original.file_type().is_symlink(),
        "Source save is a linked folder."
    );
    let source = resolve(&std::env::current_dir()?, &source.to_string_lossy())?;
    ensure!(
        source.file_name().is_some_and(|s| s == "SPRJ0005"),
        "Select SPRJ0005 itself."
    );
    ensure!(
        fs::read_dir(&source)?
            .filter_map(|e| e.ok())
            .any(|e| e.path().is_file()
                && (e.file_name().to_string_lossy().starts_with("userdata")
                    || e.path().extension().is_some_and(|s| s == "dat"))),
        "No decrypted userdata or .dat files found."
    );
    let title = source
        .parent()
        .and_then(Path::file_name)
        .and_then(|s| s.to_str())
        .filter(|s| Regex::new(r"^CUSA\d{5}$").unwrap().is_match(s))
        .unwrap_or("CUSA00207");
    let requested = user.join("savedata/1").join(title).join("SPRJ0005");
    if requested.exists() {
        let m = fs::symlink_metadata(&requested)?;
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            ensure!(
                m.file_attributes() & 0x400 == 0,
                "Destination save is linked."
            );
        }
        ensure!(!m.file_type().is_symlink(), "Destination save is linked.");
    }
    let target = resolve(&std::env::current_dir()?, &requested.to_string_lossy())?;
    ensure!(
        !within(&target, &source) && !within(&source, &target),
        "Source and destination saves must be separate."
    );
    let parent = target.parent().context("Invalid save target")?;
    fs::create_dir_all(parent)?;
    let staging = tempfile::Builder::new()
        .prefix(".save-import-")
        .tempdir_in(parent)?;
    ensure!(
        staging.path().parent() == Some(parent),
        "Staging escaped its intended destination."
    );
    copy_tree(&source, &staging.path().join("SPRJ0005"))?;
    let backup = if target.exists() {
        let m = fs::symlink_metadata(&target)?;
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            ensure!(
                m.file_attributes() & 0x400 == 0,
                "Destination save is a linked folder."
            );
        }
        ensure!(!m.file_type().is_symlink(), "Destination save is linked.");
        let backup = parent.join(format!(
            "SPRJ0005.backup-{}",
            Local::now().format("%Y%m%d-%H%M%S-%f")
        ));
        fs::rename(&target, &backup)?;
        Some(backup)
    } else {
        None
    };
    if let Err(e) = fs::rename(staging.path().join("SPRJ0005"), &target) {
        if let Some(b) = &backup {
            fs::rename(b, &target).context("Could not restore save backup")?;
        }
        bail!(e);
    }
    Ok((target, backup))
}
pub fn find_root() -> Result<PathBuf> {
    for start in [
        std::env::current_exe()?.parent().map(Path::to_path_buf),
        Some(std::env::current_dir()?),
    ]
    .into_iter()
    .flatten()
    {
        for p in start.ancestors() {
            if p.join("run-windows.ps1").is_file() {
                return Ok(p.into());
            }
        }
    }
    bail!("Keep the launcher inside the bbport checkout.")
}
