use bloodborne_launcher::{model::*, session};
use std::fs;
fn root() -> (tempfile::TempDir, Preferences) {
    let t = tempfile::tempdir().unwrap();
    let p = Preferences::load(t.path()).unwrap();
    (t, p)
}
fn game(root: &std::path::Path) -> std::path::PathBuf {
    let game = root.join("game");
    fs::create_dir_all(game.join("sce_sys")).unwrap();
    let keys = b"TITLE_ID\0APP_VER\0";
    let values = b"CUSA03173\x0001.09\0";
    let mut b = b"\0PSF".to_vec();
    for n in [0x101u32, 52, 52 + keys.len() as u32, 2] {
        b.extend(n.to_le_bytes());
    }
    for (key, len, off) in [(0u16, 10u32, 0u32), (9, 6, 10)] {
        b.extend(key.to_le_bytes());
        b.extend(0x204u16.to_le_bytes());
        for n in [len, len, off] {
            b.extend(n.to_le_bytes());
        }
    }
    b.extend(keys);
    b.extend(values);
    fs::write(game.join("sce_sys/param.sfo"), b).unwrap();
    fs::write(game.join("eboot.bin"), b"test fixture").unwrap();
    game
}

#[test]
fn metadata_and_shared_defaults() {
    let b = bindings();
    assert_eq!(b.len(), 17);
    assert_eq!(b.iter().find(|b| b.key == "r1").unwrap().defaults, [32, -1]);
    let t = tempfile::tempdir().unwrap();
    let m = metadata(&game(t.path())).unwrap();
    assert_eq!(m["TITLE_ID"], "CUSA03173");
    assert_eq!(m["APP_VER"], "01.09");
}
#[test]
fn vertex_fix_is_opt_in_and_survives_overlay_changes() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("bbport.ini");
    let mut p = Profile::load(path.clone()).unwrap();
    assert!(!p.flag("vertex_explosion_fix"));
    p.set("vertex_explosion_fix", "1");
    p.save().unwrap();
    assert!(Profile::load(path.clone()).unwrap().flag("vertex_explosion_fix"));
    fs::write(&path, "vertex_explosion_fix=0\nfuture=keep\n").unwrap();
    p.set("sharpness", "0.5");
    p.save().unwrap();
    assert!(!p.flag("vertex_explosion_fix"));
    assert_eq!(read_ini(&path).unwrap()["future"], "keep");
}
#[test]
fn preserve_external_settings_comments_and_unknown() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("bbport.ini");
    fs::write(
        &path,
        "# custom\nupscaler=dlss\nfg_multiplier=2\nfuture=keep\n",
    )
    .unwrap();
    let mut p = Profile::load(path.clone()).unwrap();
    p.set("sharpness", "0.5");
    fs::write(
        &path,
        "# custom\nupscaler=dlss\nfg_multiplier=6\nfuture=keep\n",
    )
    .unwrap();
    p.save().unwrap();
    assert_eq!(p.get("fg_multiplier"), "6");
    assert_eq!(read_ini(&path).unwrap()["future"], "keep");
    assert!(fs::read_to_string(path).unwrap().starts_with("# custom\n"));
}
#[test]
fn conflicts_do_not_write() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("bbport.ini");
    fs::write(&path, "fg_multiplier=2\n").unwrap();
    let mut p = Profile::load(path.clone()).unwrap();
    p.set("fg_multiplier", "4");
    fs::write(&path, "fg_multiplier=6\n").unwrap();
    assert!(p.save().is_err());
    assert_eq!(read_ini(&path).unwrap()["fg_multiplier"], "6");
}
#[test]
fn externally_added_key_is_not_reset() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("bbport.ini");
    let mut p = Profile::load(path.clone()).unwrap();
    fs::write(&path, "frame_generation=1\n").unwrap();
    p.save().unwrap();
    assert_eq!(p.get("frame_generation"), "1");
}
#[test]
fn rebind_moves_duplicates_and_reserves_overlay_keys() {
    let t = tempfile::tempdir().unwrap();
    let mut p = Profile::load(t.path().join("bbport.ini")).unwrap();
    p.rebind("bind_cross", -1).unwrap();
    assert_eq!(p.get("bind_r1_alt"), "0");
    assert!(p.rebind("bind_cross", 73).is_err());
    assert!(p.rebind("bind_cross", 65).is_err());
    assert!(p.rebind("bind_cross", 68).is_err());
    assert!(p.rebind("bind_unknown", 4).is_err());
}
#[test]
fn save_import_preserves_source_and_backs_up_target() {
    let t = tempfile::tempdir().unwrap();
    let s = t.path().join("source/CUSA00207/SPRJ0005");
    fs::create_dir_all(&s).unwrap();
    fs::write(s.join("userdata0000"), b"new").unwrap();
    let u = t.path().join("user");
    let (target, b) = import_save(&s, &u).unwrap();
    assert!(b.is_none());
    fs::write(target.join("userdata0000"), b"old").unwrap();
    let (_, b) = import_save(&s, &u).unwrap();
    assert_eq!(fs::read(b.unwrap().join("userdata0000")).unwrap(), b"old");
    assert_eq!(fs::read(s.join("userdata0000")).unwrap(), b"new");
    assert!(import_save(&target, &u).is_err());
}
#[test]
fn launch_arguments_are_literal_and_ini_has_precedence() {
    let (root, mut p) = root();
    let t = tempfile::tempdir().unwrap();
    let game = t.path().join("O'Brien & Unicode 游戏");
    fs::create_dir(&game).unwrap();
    p.game = game.to_string_lossy().into();
    let args = session::arguments(root.path(), &p, &t.path().join("log")).unwrap();
    assert!(args.contains(&game.into_os_string()));
    assert!(!args.contains(&"-Command".into()));
    for flag in ["-Upscaler", "-OutputResolution", "-ShowFps", "-SkipIntro"] {
        assert!(!args.contains(&flag.into()));
    }
}
#[test]
fn custom_launch_options_override_without_duplicate_parameters() {
    let (root, mut p) = root();
    p.game = root.path().join("game").to_string_lossy().into();
    let prefix = root.path().join("log");
    let args = session::arguments(root.path(), &p, &prefix).unwrap();
    let model = args.iter().position(|arg| arg == "-MemoryModel").unwrap();
    assert_eq!(args[model + 1], "new");
    p.launch_options = "-memorymodel old -FpsLimit 90 -WindowMode borderless -SkipIntro".into();
    let args = session::arguments(root.path(), &p, &prefix).unwrap();
    for (name, value) in [("-MemoryModel", "old"), ("-FpsLimit", "90"), ("-WindowMode", "borderless")] {
        assert_eq!(args.iter().filter(|arg| *arg == name).count(), 1);
        let index = args.iter().position(|arg| arg == name).unwrap();
        assert_eq!(args[index + 1], value);
    }
    assert!(args.contains(&"-SkipIntro".into()));
}
#[test]
fn custom_launch_options_preserve_quoted_paths_and_reject_malformed_input() {
    let text = r#"-DlssDirectory "C:\O'Brien & $(literal)\models\" -FpsLimit -1 -ShowFps"#;
    let options = session::parse_launch_options(text).unwrap();
    assert_eq!(options[0].1.as_deref(), Some(r"C:\O'Brien & $(literal)\models\"));
    assert_eq!(options[1].1.as_deref(), Some("-1"));
    assert_eq!(options[2], ("-ShowFps", None));
    assert!(session::parse_launch_options("  \t\n").unwrap().is_empty());
    for invalid in ["-MemoryModel", "-MemoryModel bogus", "-MemoryModel old -MemoryModel new",
                    "-FpsLimit -ShowFps", "-WindowMode \"fullscreen", "-Command calc",
                    "-NoProfile", "echo hi"] {
        assert!(session::parse_launch_options(invalid).is_err(), "{invalid}");
    }
}
#[test]
fn custom_launch_options_persist_and_old_preferences_load() {
    let (root, mut p) = root();
    assert!(p.launch_options.is_empty());
    p.launch_options = "-MemoryModel old".into();
    p.save(root.path()).unwrap();
    assert_eq!(Preferences::load(root.path()).unwrap().launch_options, p.launch_options);
    let path = root.path().join("out/windows-launcher/settings.json");
    let mut legacy: serde_json::Value = serde_json::from_str(&fs::read_to_string(&path).unwrap()).unwrap();
    legacy.as_object_mut().unwrap().remove("launch_options");
    fs::write(path, serde_json::to_string(&legacy).unwrap()).unwrap();
    assert!(Preferences::load(root.path()).unwrap().launch_options.is_empty());
}
#[cfg(windows)]
#[test]
fn custom_launch_options_reach_powershell_as_literal_parameters() {
    use std::os::windows::process::CommandExt;
    let (root, mut p) = root();
    p.game = root.path().join("game").to_string_lossy().into();
    p.launch_options = r#"-MemoryModel old -FpsLimit 90 -DlssDirectory "C:\O'Brien & $(literal)\models\" -ShowFps"#.into();
    fs::write(root.path().join("run-windows.ps1"), r#"
param($GameDirectory,$BuildDirectory,$OutputDirectory,$UserDirectory,$MemoryModel,
      $WindowResolution,$PresentMode,$FrameGeneration,$FpsPatch,$FpsLimit,$Timeout,
      $GameLogPrefix,$DlssDirectory,[switch]$Gpu,[switch]$SkipBuild,[switch]$BaseOnly,
      [switch]$ShowFps)
@{MemoryModel=$MemoryModel; FpsLimit=$FpsLimit; DlssDirectory=$DlssDirectory;
  ShowFps=$ShowFps.IsPresent} | ConvertTo-Json -Compress
"#).unwrap();
    let output = std::process::Command::new("powershell.exe")
        .creation_flags(0x08000000)
        .args(session::arguments(root.path(), &p, &root.path().join("log")).unwrap())
        .output().unwrap();
    assert!(output.status.success(), "{}", String::from_utf8_lossy(&output.stderr));
    let values: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(values["MemoryModel"], "old");
    assert_eq!(values["FpsLimit"], "90");
    assert_eq!(values["DlssDirectory"], r"C:\O'Brien & $(literal)\models\");
    assert_eq!(values["ShowFps"], true);
}
#[test]
fn corrupt_sfo_is_rejected() {
    let t = tempfile::tempdir().unwrap();
    fs::create_dir(t.path().join("sce_sys")).unwrap();
    fs::write(t.path().join("sce_sys/param.sfo"), b"short").unwrap();
    assert!(metadata(t.path()).is_err());
}
#[test]
fn fg_and_paths_and_finite_values_are_checked() {
    let (t, mut p) = root();
    p.game = game(t.path()).to_string_lossy().into();
    let build = t.path().join("exe");
    fs::create_dir(&build).unwrap();
    for f in [
        "bb-probe.exe",
        "bb-game.exe",
        "nvngx_dlss.dll",
        "sl.interposer.dll",
        ".bbport-dlssg-game",
    ] {
        fs::write(build.join(f), b"fixture").unwrap();
    }
    p.build = build.to_string_lossy().into();
    p.engine = "menu-sm86".into();
    p.present = "Immediate".into();
    let mut v = Profile::load(t.path().join("profile/bbport.ini")).unwrap();
    v.set("upscaler", "dlss");
    v.set("frame_generation", "1");
    validate(t.path(), &p, &v, true).unwrap();
    for (key, value) in [
        ("upscaler", "fsr3"),
        ("fg_multiplier", "9"),
        ("reflex", "invalid"),
        ("window_mode", "invalid"),
        ("mouse_sensitivity", "NaN"),
        ("sharpness", "inf"),
    ] {
        let mut changed = v.clone();
        changed.set(key, value);
        assert!(validate(t.path(), &p, &changed, true).is_err());
    }
    p.output = format!("{}/runtime", p.game);
    assert!(validate(t.path(), &p, &v, false).is_err());
}
#[test]
fn migrated_preferences_roundtrip() {
    let (t, mut p) = root();
    p.engine = "menu-sm86".into();
    p.fps_limit = 144;
    p.fullscreen = true;
    p.save(t.path()).unwrap();
    let restored = Preferences::load(t.path()).unwrap();
    assert_eq!(restored.engine, "menu-sm86");
    assert_eq!(restored.fps_limit, 144);
    assert!(restored.fullscreen);
}

#[test]
fn window_modes_migrate_and_preserve_insert_changes() {
    let t = tempfile::tempdir().unwrap();
    let path = t.path().join("bbport.ini");
    let mut v = Profile::load(path.clone()).unwrap();
    v.migrate_fullscreen(true);
    assert_eq!(v.get("window_mode"), "borderless");
    v.save().unwrap();
    fs::write(&path, "window_mode=fullscreen\n").unwrap();
    v = Profile::load(path).unwrap();
    v.migrate_fullscreen(true);
    assert_eq!(v.get("window_mode"), "fullscreen");
    v.set("sharpness", "0.5");
    v.save().unwrap();
    assert_eq!(v.get("window_mode"), "fullscreen");
}

#[test]
fn mods_detect_toggle_order_and_preferences_roundtrip() {
    use bloodborne_launcher::mods;
    let (t, mut prefs) = root();
    prefs.game = game(t.path()).to_string_lossy().into();
    for (name, layout) in [
        ("Wrapped", "app0/dvdroot_ps4/chr"),
        ("Bare", "map"),
        ("Title", "CUSA00207/dvdroot_ps4/param"),
        ("Archive", "v2/chr"),
    ] {
        let folder = t.path().join(name);
        fs::create_dir_all(folder.join(layout)).unwrap();
        fs::write(folder.join(layout).join("a.dcx"), b"fixture").unwrap();
        let entry = mods::inspect(&folder).unwrap();
        assert_eq!(entry.files, 1);
        assert_eq!(entry.folders.len(), 1);
        mods::add(&mut prefs.mods, entry.clone()).unwrap();
        assert!(mods::add(&mut prefs.mods, entry).is_err());
    }
    prefs.mods[0].enabled = false;
    mods::move_entry(&mut prefs.mods, 0, 1);
    assert_eq!(prefs.mods[1].name, "Wrapped");
    assert!(!prefs.mods[1].enabled);
    prefs.mods_enabled = false;
    prefs.save(t.path()).unwrap();
    let restored = Preferences::load(t.path()).unwrap();
    assert!(!restored.mods_enabled);
    assert!(!restored.mods[1].enabled);
    assert_eq!(restored.mods.len(), 4);
    let args = session::arguments(t.path(), &restored, &t.path().join("session")).unwrap();
    assert!(!args.iter().any(|v| v == "-ModsConfig"));
    prefs.mods_enabled = true;
    let args = session::arguments(t.path(), &prefs, &t.path().join("session")).unwrap();
    assert!(args.iter().any(|v| v == "-ModsConfig"));
    assert!(
        args.iter()
            .any(|v| v == t.path().join("session.mods.json").as_os_str())
    );
}

#[test]
fn mod_detection_rejects_empty_and_unknown() {
    use bloodborne_launcher::mods;
    let t = tempfile::tempdir().unwrap();
    fs::create_dir_all(t.path().join("Empty/dvdroot_ps4")).unwrap();
    assert!(mods::inspect(&t.path().join("Empty")).is_err());
    fs::create_dir_all(t.path().join("Junk/stuff")).unwrap();
    fs::write(t.path().join("Junk/stuff/a.txt"), b"not a mod").unwrap();
    assert!(mods::inspect(&t.path().join("Junk")).is_err());
}
#[test]
fn capture_codes_match_sdl_physical_and_mouse_ids() {
    use bloodborne_launcher::capture::code;
    assert_eq!(code(65), Some(4));
    assert_eq!(code(160), Some(225));
    assert_eq!(code(161), Some(229));
    assert_eq!(code(1), Some(-1));
    assert_eq!(code(2), Some(-3));
    assert_eq!(code(4), Some(-2));
    assert_eq!(code(119), Some(65));
    assert_eq!(code(45), Some(73));
    assert_eq!(code(0), None);
}
