// SPDX-License-Identifier: GPL-2.0-or-later
use crate::model::{self, Preferences};
use anyhow::{Context, Result, bail, ensure};
use chrono::Local;
use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::OsString,
    fs::{self, File},
    io::{Read, Seek, SeekFrom},
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
};

/// Split literal runner arguments, preserving Windows paths and quoted spaces.
/// This is argument data for PowerShell -File, never a PowerShell expression.
pub fn parse_launch_options(text: &str) -> Result<Vec<(&'static str, Option<String>)>> {
    let mut words = Vec::new();
    let mut word = String::new();
    let mut quote = None;
    let mut started = false;
    let mut chars = text.chars().peekable();
    while let Some(ch) = chars.next() {
        ensure!(ch != '\0', "Launch options cannot contain a null character.");
        if let Some(delimiter) = quote {
            if ch == delimiter {
                if chars.peek() == Some(&delimiter) {
                    chars.next();
                    word.push(ch);
                } else {
                    quote = None;
                }
            } else {
                word.push(ch);
            }
        } else if ch == '"' || ch == '\'' {
            quote = Some(ch);
            started = true;
        } else if ch.is_whitespace() {
            if started {
                words.push(std::mem::take(&mut word));
                started = false;
            }
        } else {
            word.push(ch);
            started = true;
        }
    }
    ensure!(quote.is_none(), "Close the quotation mark in launch options.");
    if started {
        words.push(word);
    }
    const OPTIONS: &[&str] = &[
        "-MemoryModel", "-FpsLimit", "-FpsPatch", "-Timeout", "-Resolution",
        "-OutputResolution", "-WindowResolution", "-WindowMode", "-ResolutionMode",
        "-PresentMode", "-Upscaler", "-UpscalePreset", "-Reflex", "-FrameGeneration",
        "-DlssDirectory", "-StreamlineDirectory", "-ShowFps", "-SkipIntro",
    ];
    let mut parsed = Vec::new();
    let mut seen = BTreeSet::new();
    let mut words = words.into_iter();
    while let Some(word) = words.next() {
        let Some(&name) = OPTIONS.iter().find(|name| name.eq_ignore_ascii_case(&word)) else {
            bail!("Unsupported launch option: {word}. Use runner options such as -MemoryModel old.");
        };
        ensure!(seen.insert(name), "Launch option specified twice: {name}");
        if matches!(name, "-ShowFps" | "-SkipIntro") {
            parsed.push((name, None));
            continue;
        }
        let value = words.next().with_context(|| format!("Enter a value after {name}."))?;
        ensure!(
            !value.is_empty() && (!value.starts_with('-') || value.parse::<i64>().is_ok()),
            "Enter a value after {name}."
        );
        if name == "-MemoryModel" {
            ensure!(["auto", "old", "new"].iter().any(|v| v.eq_ignore_ascii_case(&value)),
                    "MemoryModel must be new, old or auto.");
        }
        parsed.push((name, Some(value)));
    }
    Ok(parsed)
}

pub fn arguments(root: &Path, p: &Preferences, prefix: &Path) -> Result<Vec<OsString>> {
    let mut args: Vec<OsString> = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"]
        .map(Into::into)
        .to_vec();
    args.push(root.join("run-windows.ps1").into_os_string());
    args.extend(["-Gpu", "-SkipBuild"].map(Into::into));
    args.extend(["-MemoryModel", "new"].map(Into::into));
    for (name, value) in [
        ("-GameDirectory", &p.game),
        ("-BuildDirectory", &p.build),
        ("-OutputDirectory", &p.output),
        ("-UserDirectory", &p.user),
    ] {
        args.push(name.into());
        args.push(model::resolve(root, value)?.into_os_string());
    }
    for (name, value) in [
        ("-WindowResolution", p.window.as_str()),
        ("-PresentMode", &p.present),
        ("-FrameGeneration", &p.engine),
        ("-FpsPatch", &p.fps_patch),
    ] {
        args.extend([name.into(), value.into()]);
    }
    args.extend([
        "-FpsLimit".into(),
        p.fps_limit.to_string().into(),
        "-Timeout".into(),
        "0".into(),
    ]);
    if p.use_patch {
        args.push("-PatchDirectory".into());
        args.push(model::resolve(root, &p.patch)?.into_os_string());
    } else {
        args.push("-BaseOnly".into());
    }
    args.extend(["-GameLogPrefix".into(), prefix.as_os_str().to_owned()]);
    if p.mods_enabled && p.mods.iter().any(|m| m.enabled) {
        args.extend([
            "-ModsConfig".into(),
            prefix.with_extension("mods.json").into_os_string(),
        ]);
    }
    for (name, value) in parse_launch_options(&p.launch_options)? {
        // Replace generated options instead of passing a duplicate named parameter to -File.
        if let Some(index) = args.iter().position(|arg| arg.to_string_lossy().eq_ignore_ascii_case(name)) {
            args.drain(index..index + if value.is_some() { 2 } else { 1 });
        }
        args.push(name.into());
        if let Some(value) = value {
            args.push(value.into());
        }
    }
    Ok(args)
}
pub struct Session {
    pub child: Child,
    pub log_dir: PathBuf,
    pub preparation: PathBuf,
    pub game_prefix: String,
    pub stop: PathBuf,
    pub stopped: bool,
    offsets: BTreeMap<PathBuf, u64>,
    pub last_log: String,
}
impl Session {
    pub fn start(root: &Path, p: &Preferences) -> Result<Self> {
        let log_dir = model::resolve(root, &p.output)?.join("logs");
        fs::create_dir_all(&log_dir)?;
        let stamp = Local::now().format("%Y%m%d-%H%M%S-%f").to_string();
        let preparation = log_dir.join(format!("launcher-{stamp}.log"));
        let game_prefix = format!("game-{stamp}");
        let prefix = log_dir.join(&game_prefix);
        if p.mods_enabled && p.mods.iter().any(|m| m.enabled) {
            let mut mods = p.mods.clone();
            for entry in &mut mods {
                if entry.enabled {
                    entry.path = model::resolve(root, &entry.path)?.to_string_lossy().into();
                }
            }
            model::atomic_write(
                &prefix.with_extension("mods.json"),
                &(serde_json::to_string_pretty(
                    &serde_json::json!({"enabled": true, "mods": mods}),
                )? + "\n"),
            )?;
        }
        let stop = log_dir.join(format!("stop-{stamp}.trigger"));
        let log = File::create(&preparation)?;
        let mut command = Command::new("powershell.exe");
        command
            .args(arguments(root, p, &prefix)?)
            .current_dir(root)
            .stdin(Stdio::null())
            .stdout(Stdio::from(log.try_clone()?))
            .stderr(Stdio::from(log));
        for (k, _) in std::env::vars_os() {
            if k.to_string_lossy().to_uppercase().starts_with("BB_") {
                command.env_remove(k);
            }
        }
        command
            .env("BB_STOP_FILE", &stop)
            .env("PYTHONUTF8", "1")
            .env("PYTHONIOENCODING", "utf-8");
        #[cfg(feature = "ui-validation")]
        if std::env::args().any(|s| s == "--game-smoke") {
            let pad = model::resolve(root, &p.output)?.join("pad.txt");
            anyhow::ensure!(
                pad.starts_with(root.join("out")) && pad.is_file(),
                "Test pad must belong to an out/ fixture."
            );
            command
                .env("BB_PAD_FILE", pad)
                .env("BB_PAD_SCRIPT_ONLY", "1")
                .env("BB_FRAME_STATS", "0")
                .env("BB_GPU_PROFILE", "0");
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        let child = command.spawn().context("Start the Windows runner")?;
        Ok(Self {
            child,
            log_dir,
            preparation,
            game_prefix,
            stop,
            stopped: false,
            offsets: BTreeMap::new(),
            last_log: String::new(),
        })
    }
    pub fn stop(&mut self) -> Result<()> {
        fs::write(&self.stop, b"stop\n")?;
        self.stopped = true;
        Ok(())
    }
    pub fn poll(&mut self) -> Result<(bool, Option<i32>)> {
        let mut paths = vec![self.preparation.clone()];
        paths.extend(
            fs::read_dir(&self.log_dir)?
                .filter_map(|e| e.ok().map(|e| e.path()))
                .filter(|p| {
                    p.extension().is_some_and(|extension| extension == "log")
                        && p.file_name()
                            .is_some_and(|n| n.to_string_lossy().starts_with(&self.game_prefix))
                }),
        );
        paths.sort();
        let mut changed = false;
        for path in paths {
            let mut file = File::open(&path)?;
            let offset = self.offsets.entry(path).or_default();
            file.seek(SeekFrom::Start(*offset))?;
            let mut bytes = Vec::new();
            file.take(128 * 1024).read_to_end(&mut bytes)?;
            *offset += bytes.len() as u64;
            if !bytes.is_empty() {
                changed = true;
                self.last_log.push_str(&String::from_utf8_lossy(&bytes));
            }
        }
        if self.last_log.len() > 180_000 {
            let mut cut = self.last_log.len() - 140_000;
            while !self.last_log.is_char_boundary(cut) {
                cut += 1;
            }
            self.last_log.drain(..cut);
        }
        let code = self.child.try_wait()?.map(|v| v.code().unwrap_or(-1));
        if code.is_some() {
            let _ = fs::remove_file(&self.stop);
        }
        Ok((changed, code))
    }
}
