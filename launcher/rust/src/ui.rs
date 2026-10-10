// SPDX-License-Identifier: GPL-2.0-or-later
use crate::motion::{ButtonKind, ButtonMotionExt, Surface};
use anyhow::{Result, ensure};
use bloodborne_launcher::capture;
use bloodborne_launcher::{
    model::{self, Preferences, Profile},
    mods::{self, ModEntry},
    session::Session,
};
use gpui_kit::assets::IconName;
use gpui_kit::component::{
    button::*,
    checkbox::Checkbox,
    input::{Input, InputEvent, InputState},
    select::{Select, SelectEvent, SelectState},
    switch::Switch,
    *,
};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
#[cfg(feature = "ui-validation")]
use gpui_kit::base::TestSupportExt;
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
    sync::mpsc,
    time::{Duration, Instant},
};

type Picker = SelectState<Vec<SharedString>>;
struct Choice {
    state: Entity<Picker>,
    options: Vec<(String, String)>,
}
enum Job {
    Folder(String, Option<PathBuf>),
    Import(Result<String>),
    AddMod(Result<Option<ModEntry>>),
    RefreshMods(Result<Vec<ModEntry>>),
}
const PAGES: &[(&str, &str)] = &[
    ("Play", "Your next hunt starts here."),
    (
        "Graphics",
        "Resolution, reconstruction and frame generation.",
    ),
    (
        "Game Effects",
        "Tune the game's appearance before your next session.",
    ),
    (
        "Keyboard & Mouse",
        "Your controller, mapped to your keyboard and mouse.",
    ),
    (
        "Session Log",
        "Preparation and game output, together in one place.",
    ),
    ("Mods", "Customize your hunt with loose-file mods."),
];
pub struct Launcher {
    root: PathBuf,
    prefs: Preferences,
    profile: Profile,
    inputs: BTreeMap<String, Entity<InputState>>,
    choices: BTreeMap<String, Choice>,
    subscriptions: Vec<Subscription>,
    page: usize,
    page_revision: usize,
    status: SharedString,
    error: bool,
    session: Option<Session>,
    log: SharedString,
    log_dir: Option<PathBuf>,
    selected: String,
    selected_revision: usize,
    capture: Option<(String, Vec<i32>)>,
    jobs: mpsc::Receiver<Job>,
    sender: mpsc::Sender<Job>,
    busy: bool,
    last_poll: Instant,
    _ticker: Task<()>,
    focus: FocusHandle,
    fps_text: String,
    game_info: SharedString,
    scroll: [ScrollHandle; 6],
    #[cfg(feature = "ui-validation")]
    smoke_case: Option<PathBuf>,
    #[cfg(feature = "ui-validation")]
    smoke_mod_profile: Option<PathBuf>,
}
impl Launcher {
    #[cfg(feature = "ui-validation")]
    pub fn smoke_preferences(&self) -> &Preferences {
        &self.prefs
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_effect_profile(&mut self, path: PathBuf, cx: &mut Context<Self>) {
        self.profile = Profile::load(path).expect("isolated effects profile");
        self.smoke_page(2, 0., cx);
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_vertex_fix(&self) -> bool {
        self.profile.flag("vertex_explosion_fix")
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_save_reload_effects(&mut self) -> bool {
        self.profile.save().expect("save isolated effects");
        self.profile = Profile::load(self.profile.path.clone()).expect("reload effects");
        self.smoke_vertex_fix()
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_scroll_to_bottom(&self, cx: &mut Context<Self>) {
        let maximum = self.scroll[self.page].max_offset();
        self.scroll[self.page].set_offset(point(px(0.), -maximum.y));
        cx.notify();
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_selection(&self) -> &str {
        &self.selected
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_current_page(&self) -> usize {
        self.page
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_use_patch(&self) -> bool {
        self.prefs.use_patch
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_window_mode(&self) -> &str {
        self.profile.get("window_mode")
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_mods(&mut self, entries: Vec<ModEntry>, profile: PathBuf, cx: &mut Context<Self>) {
        self.prefs.mods = entries;
        self.prefs.mods_enabled = true;
        self.smoke_mod_profile = Some(profile);
        self.navigate(5, cx);
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_mod_entries(&self) -> (&[ModEntry], bool) {
        (&self.prefs.mods, self.prefs.mods_enabled)
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_page(&mut self, page: usize, offset: f32, cx: &mut Context<Self>) {
        self.navigate(page, cx);
        self.scroll[page].set_offset(point(px(0.), px(-offset)));
        cx.notify();
    }
    fn navigate(&mut self, page: usize, cx: &mut Context<Self>) {
        if self.page != page {
            self.page = page;
            self.page_revision += 1;
            cx.notify();
        }
    }
    fn select_control(&mut self, key: String, cx: &mut Context<Self>) {
        if self.selected != key {
            self.selected = key;
            self.selected_revision += 1;
            cx.notify();
        }
    }
    #[cfg(feature = "ui-validation")]
    pub fn smoke_begin(
        &mut self,
        case: PathBuf,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) -> Result<()> {
        let case = model::resolve(&self.root, &case.to_string_lossy())?;
        ensure!(
            case.starts_with(self.root.join("out")) && case.join("pad.txt").is_file(),
            "Use an existing out/ test fixture."
        );
        self.prefs.output = case.to_string_lossy().into();
        self.prefs.user = case.join("user").to_string_lossy().into();
        self.prefs.build = self
            .root
            .join("out/windows-fg-game")
            .to_string_lossy()
            .into();
        if case.join("runtime-path.txt").is_file() {
            let runtime = model::resolve(
                &self.root,
                fs::read_to_string(case.join("runtime-path.txt"))?.trim(),
            )?;
            ensure!(
                runtime.starts_with(self.root.join("out")),
                "Use an out/ test runtime."
            );
            self.prefs.build = runtime.to_string_lossy().into();
        }
        self.prefs.engine = "menu-sm86".into();
        self.prefs.present = "Immediate".into();
        self.prefs.window = "1280x720".into();
        self.prefs.fullscreen = false;
        self.fps_text = "120".into();
        self.profile = Profile::load(case.join("bbport.ini"))?;
        self.smoke_case = Some(case);
        self.sync(window, cx);
        self.launch(window, cx);
        ensure!(self.session.is_some(), "{}", self.status);
        Ok(())
    }
    pub fn new(root: PathBuf, window: &mut Window, cx: &mut Context<Self>) -> Result<Self> {
        let mut prefs = Preferences::load(&root)?;
        let mut profile = Profile::load(model::resolve(&root, &prefs.output)?.join("bbport.ini"))?;
        profile.migrate_fullscreen(prefs.fullscreen);
        prefs.fullscreen = false;
        let (sender, jobs) = mpsc::channel();
        let ticker = cx.spawn_in(window, async move |this, window| {
            loop {
                window
                    .background_executor()
                    .timer(Duration::from_millis(40))
                    .await;
                if this
                    .update_in(window, |this, window, cx| this.tick(window, cx))
                    .is_err()
                {
                    break;
                }
            }
        });
        let fps_text = prefs.fps_limit.to_string();
        let mut this = Self {
            root,
            prefs,
            profile,
            inputs: BTreeMap::new(),
            choices: BTreeMap::new(),
            subscriptions: Vec::new(),
            page: 0,
            page_revision: 0,
            status: "Ready for the hunt".into(),
            error: false,
            session: None,
            log: "No session started yet.".into(),
            log_dir: None,
            selected: String::new(),
            selected_revision: 0,
            capture: None,
            jobs,
            sender,
            busy: false,
            last_poll: Instant::now(),
            _ticker: ticker,
            focus: cx.focus_handle(),
            fps_text,
            game_info: "Game folder not selected".into(),
            scroll: std::array::from_fn(|_| ScrollHandle::new()),
            #[cfg(feature = "ui-validation")]
            smoke_case: None,
            #[cfg(feature = "ui-validation")]
            smoke_mod_profile: None,
        };
        for key in [
            "@game",
            "@patch",
            "@user",
            "@output",
            "@build",
            "@fps_limit",
            "@launch_options",
            "sharpness",
            "mouse_sensitivity",
        ] {
            let state = cx.new(|cx| {
                let state = InputState::new(window, cx);
                if key == "@launch_options" {
                    state.placeholder("e.g. -MemoryModel old -FpsLimit 90")
                } else {
                    state
                }
            });
            this.subscriptions.push(cx.subscribe_in(&state, window, {
                let key = key.to_owned();
                move |this, state, event, _, cx| {
                    if matches!(event, InputEvent::Change) {
                        let value = state.read(cx).value().to_string();
                        if let Some(k) = key.strip_prefix('@') {
                            if k == "fps_limit" {
                                this.fps_text = value;
                            } else if k == "launch_options" {
                                this.prefs.launch_options = value;
                            } else {
                                this.prefs.set_folder(k, value);
                                if k == "game" {
                                    this.refresh_game_info();
                                }
                            }
                        } else {
                            this.profile.set(&key, value);
                        }
                        cx.notify();
                    }
                }
            }));
            this.inputs.insert(key.into(), state);
        }
        let mut options: Vec<(&str, Vec<(String, String)>)> = vec![
            (
                "upscaler",
                pairs(&[
                    ("Off", "off"),
                    ("DLSS (NVIDIA)", "dlss"),
                    ("FSR 3.1", "fsr3"),
                    ("FSR 4 (INT8)", "fsr4"),
                    ("FSR 4.1.1", "fsr411"),
                    ("Native TAA", "taa"),
                ]),
            ),
            (
                "preset",
                pairs(&[
                    ("Native AA", "0"),
                    ("Quality", "1"),
                    ("Balanced", "2"),
                    ("Performance", "3"),
                    ("Ultra Performance", "4"),
                ]),
            ),
            ("output_res", same(model::RESOLUTIONS)),
            ("@window", same(model::RESOLUTIONS)),
            (
                "window_mode",
                pairs(&[
                    ("Windowed", "windowed"),
                    ("Borderless Fullscreen", "borderless"),
                    ("Fullscreen", "fullscreen"),
                ]),
            ),
            (
                "live_resolution",
                pairs(&[
                    ("Startup (restart required)", "0"),
                    ("Live", "1"),
                    ("Auto", "auto"),
                ]),
            ),
            (
                "@engine",
                pairs(&[
                    ("Standard Windows", "off"),
                    ("SM86 (RTX 20 / 30)", "menu-sm86"),
                    ("Official NVIDIA FG", "menu"),
                ]),
            ),
            (
                "fg_multiplier",
                pairs(&[
                    ("2X", "2"),
                    ("3X", "3"),
                    ("4X", "4"),
                    ("5X", "5"),
                    ("6X", "6"),
                ]),
            ),
            (
                "reflex",
                pairs(&[("Off", "off"), ("On", "on"), ("On + Boost", "boost")]),
            ),
            (
                "@present",
                pairs(&[
                    ("Immediate (allow tearing)", "Immediate"),
                    ("Mailbox", "Mailbox"),
                    ("FIFO (VSync)", "Fifo"),
                ]),
            ),
            (
                "@fps_patch",
                pairs(&[
                    ("Uncapped", "uncap"),
                    ("60 FPS", "60"),
                    ("90 FPS", "90"),
                    ("Preserve game patch", "preserve"),
                ]),
            ),
            (
                "model_lod",
                pairs(&[
                    ("Highest", "-2"),
                    ("High", "-1"),
                    ("Game default", "0"),
                    ("Lower", "1"),
                    ("Lowest", "2"),
                ]),
            ),
        ];
        for (key, values) in options.drain(..) {
            this.add_choice(key.into(), values, window, cx);
        }
        for b in model::bindings() {
            for suffix in ["", "_alt"] {
                let key = format!("bind_{}{suffix}", b.key);
                let mut values: Vec<_> = model::choices()
                    .into_iter()
                    .map(|(k, c)| (k, c.to_string()))
                    .collect();
                let current = this.profile.get(&key).to_owned();
                if !values.iter().any(|(_, v)| v == &current) {
                    values.push((format!("Scancode {current}"), current));
                }
                this.add_choice(key, values, window, cx);
            }
        }
        this.sync(window, cx);
        this.refresh_game_info();
        // Decode embedded artwork off the UI thread before its first page opens.
        for artwork in [
            "launcher/brand-cover.jpg",
            "launcher/hunter-workshop.png",
            "launcher/controller-dark.svg",
        ] {
            let _ = cx.fetch_asset::<ImgResourceLoader>(&Resource::Embedded(artwork.into()));
        }
        for control in crate::controller::CONTROLS {
            let _ = cx.fetch_asset::<ImgResourceLoader>(&Resource::Embedded(
                format!("launcher/selection-{}.svg", control.key).into(),
            ));
        }
        window.focus(&this.focus, cx);
        let weak = cx.entity().downgrade();
        window.on_window_should_close(cx, move |window, cx| {
            weak.update(cx, |this, cx| {
                if this.busy {
                    this.message(
                        "Finish the current folder or save operation before closing.",
                        true,
                    );
                    cx.notify();
                    return false;
                }
                if this.session.is_none() {
                    return true;
                }
                window.open_alert_dialog(cx, |dialog, _, _| {
                    dialog
                        .title("Game is running")
                        .confirm()
                        .child("Close the launcher and leave the game running?")
                        .on_ok(|_, _, cx| {
                            cx.quit();
                            true
                        })
                });
                false
            })
            .unwrap_or(true)
        });
        Ok(this)
    }
    fn locked(&self) -> bool {
        self.session.is_some() || self.busy
    }
    fn refresh_game_info(&mut self) {
        self.game_info = model::resolve(&self.root, &self.prefs.game)
            .and_then(|p| model::metadata(&p))
            .map(|info| {
                format!(
                    "{}  ·  Version {}",
                    info.get("TITLE_ID")
                        .map(String::as_str)
                        .unwrap_or("Bloodborne"),
                    info.get("APP_VER").map(String::as_str).unwrap_or("Unknown")
                )
            })
            .unwrap_or_else(|_| "Select your Bloodborne game folder below".into())
            .into();
    }
    fn value(&self, key: &str) -> String {
        match key {
            "@window" => self.prefs.window.clone(),
            "@engine" => self.prefs.engine.clone(),
            "@present" => self.prefs.present.clone(),
            "@fps_patch" => self.prefs.fps_patch.clone(),
            "@fps_limit" => self.fps_text.clone(),
            "@launch_options" => self.prefs.launch_options.clone(),
            k if k.starts_with('@') => self.prefs.folder(&k[1..]).into(),
            k => self.profile.get(k).into(),
        }
    }
    fn add_choice(
        &mut self,
        key: String,
        options: Vec<(String, String)>,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let labels: Vec<SharedString> = options.iter().map(|(k, _)| k.clone().into()).collect();
        let index = options.iter().position(|(_, v)| v == &self.value(&key));
        let state = cx.new(|cx| {
            SelectState::new(labels, index.map(IndexPath::new), window, cx)
                .searchable(key.starts_with("bind_"))
        });
        self.subscriptions.push(cx.subscribe_in(&state, window, {
            let key = key.clone();
            let options = options.clone();
            move |this, _, event, window, cx| {
                if let SelectEvent::Confirm(Some(label)) = event
                    && let Some((_, value)) = options.iter().find(|(l, _)| l == label.as_ref())
                {
                    this.choose(&key, value, window, cx);
                }
            }
        }));
        self.choices.insert(key, Choice { state, options });
    }
    fn choose(&mut self, key: &str, value: &str, window: &mut Window, cx: &mut Context<Self>) {
        if self.locked() {
            return;
        }
        match key {
            "@window" => self.prefs.window = value.into(),
            "@present" => self.prefs.present = value.into(),
            "@fps_patch" => self.prefs.fps_patch = value.into(),
            "@engine" => {
                let old_default = ["out/windows", "out/windows-fg-game"]
                    .map(|p| self.root.join(p).to_string_lossy().into_owned());
                if old_default.contains(&self.prefs.build) {
                    self.prefs.build = self
                        .root
                        .join(if value == "off" {
                            "out/windows"
                        } else {
                            "out/windows-fg-game"
                        })
                        .to_string_lossy()
                        .into();
                }
                self.prefs.engine = value.into();
                if value != "off" {
                    self.prefs.present = "Immediate".into();
                }
            }
            k if k.starts_with("bind_") => {
                if let Err(e) = value
                    .parse::<i32>()
                    .map_err(anyhow::Error::from)
                    .and_then(|v| self.profile.rebind(k, v))
                {
                    self.message(e.to_string(), true);
                }
            }
            k => self.profile.set(k, value),
        }
        self.sync(window, cx);
        cx.notify();
    }
    fn sync(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        for (key, state) in &self.inputs {
            let value = self.value(key);
            state.update(cx, |s, cx| s.set_value(value, window, cx));
        }
        for (key, choice) in &self.choices {
            let value = self.value(key);
            if let Some((label, _)) = choice.options.iter().find(|(_, v)| v == &value) {
                let label: SharedString = label.clone().into();
                choice
                    .state
                    .update(cx, |s, cx| s.set_selected_value(&label, window, cx));
            }
        }
    }
    fn message(&mut self, text: impl Into<SharedString>, error: bool) {
        self.status = text.into();
        self.error = error;
    }
    fn save(&mut self) -> Result<()> {
        ensure!(!self.locked(), "Close the current session first.");
        ensure!(
            model::resolve(&self.root, &self.prefs.output)?.join("bbport.ini") == self.profile.path,
            "Load the selected profile before saving to a different output folder."
        );
        model::validate(&self.root, &self.prefs, &self.profile, false)?;
        self.profile.save()?;
        self.prefs.save(&self.root)?;
        Ok(())
    }
    fn save_clicked(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let value = self.inputs["@fps_limit"].read(cx).value().to_string();
        let result = value
            .parse::<u16>()
            .map_err(anyhow::Error::from)
            .and_then(|v| {
                self.prefs.fps_limit = v;
                self.save()
            });
        match result {
            Ok(()) => {
                self.sync(window, cx);
                self.message("Settings saved. Insert shares this profile.", false);
            }
            Err(e) => self.message(e.to_string(), true),
        }
        cx.notify();
    }
    fn reload(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.locked() {
            return;
        }
        if self.profile.dirty() {
            let weak = cx.entity().downgrade();
            window.open_alert_dialog(cx, move |dialog, _, _| {
                dialog
                    .title("Reload settings")
                    .confirm()
                    .child("Discard unsaved changes and load the selected profile?")
                    .on_ok({
                        let weak = weak.clone();
                        move |_, window, cx| {
                            let _ = weak.update(cx, |this, cx| this.load(window, cx));
                            true
                        }
                    })
            });
        } else {
            self.load(window, cx);
        }
    }
    fn load(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let result = model::resolve(&self.root, &self.prefs.output)
            .and_then(|p| Profile::load(p.join("bbport.ini")));
        match result {
            Ok(p) => {
                self.profile = p;
                self.sync(window, cx);
                self.refresh_game_info();
                self.message("Profile loaded.", false);
            }
            Err(e) => self.message(e.to_string(), true),
        }
        cx.notify();
    }
    fn launch(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        self.capture = None;
        self.prefs.fps_limit = match self.inputs["@fps_limit"].read(cx).value().parse() {
            Ok(v) => v,
            Err(_) => {
                self.message("FPS limit must be 0-480.", true);
                cx.notify();
                return;
            }
        };
        let result = model::validate(&self.root, &self.prefs, &self.profile, true)
            .and_then(|_| self.save())
            .and_then(|_| Session::start(&self.root, &self.prefs));
        match result {
            Ok(session) => {
                self.log_dir = Some(session.log_dir.clone());
                self.session = Some(session);
                self.log = "Preparing game files…".into();
                self.message(
                    "Preparing your hunt. The game window will open automatically.",
                    false,
                );
            }
            Err(e) => self.message(e.to_string(), true),
        }
        self.sync(window, cx);
        cx.notify();
    }
    fn browse(&mut self, key: String, cx: &mut Context<Self>) {
        if self.locked() {
            return;
        }
        let sender = self.sender.clone();
        self.busy = true;
        std::thread::spawn(move || {
            let path = rfd::FileDialog::new()
                .set_title("Select folder")
                .pick_folder();
            let _ = sender.send(Job::Folder(key, path));
        });
        cx.notify();
    }
    fn import(&mut self, cx: &mut Context<Self>) {
        if self.locked() {
            return;
        }
        if let Err(e) = model::validate(&self.root, &self.prefs, &self.profile, false) {
            self.message(e.to_string(), true);
            cx.notify();
            return;
        }
        let user = match model::resolve(&self.root, &self.prefs.user) {
            Ok(p) => p,
            Err(e) => {
                self.message(e.to_string(), true);
                return;
            }
        };
        let sender = self.sender.clone();
        self.busy = true;
        std::thread::spawn(move || {
            let result = (|| -> Result<String> {
                let Some(source) = rfd::FileDialog::new()
                    .set_title("Select decrypted SPRJ0005")
                    .pick_folder()
                else {
                    return Ok("Import cancelled.".into());
                };
                let yes=rfd::MessageDialog::new().set_title("Import save").set_description(format!("Copy {}\ninto {}?\nThe original stays intact. Existing destination saves receive a backup.",source.display(),user.display()))
                    .set_buttons(rfd::MessageButtons::YesNo).show()==rfd::MessageDialogResult::Yes;
                if !yes {
                    return Ok("Import cancelled.".into());
                }
                let (target, backup) = model::import_save(&source, &user)?;
                Ok(format!(
                    "Imported {}{}",
                    target.display(),
                    backup
                        .map(|b| format!(" · Backup: {}", b.display()))
                        .unwrap_or_default()
                ))
            })();
            let _ = sender.send(Job::Import(result));
        });
        cx.notify();
    }
    fn tick(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        #[cfg(feature = "ui-validation")]
        if self
            .smoke_case
            .as_ref()
            .is_some_and(|p| p.join("stop-session").is_file())
            && self.session.as_ref().is_some_and(|s| !s.stopped)
        {
            self.stop_session(cx);
        }
        while let Ok(job) = self.jobs.try_recv() {
            self.busy = false;
            match job {
                Job::Folder(key, Some(path)) => {
                    self.prefs.set_folder(&key, path.to_string_lossy().into());
                    if key == "game" {
                        self.refresh_game_info();
                    }
                    self.sync(window, cx);
                }
                Job::Folder(_, None) => {}
                Job::Import(result) => match result {
                    Ok(s) => self.message(s, false),
                    Err(e) => self.message(e.to_string(), true),
                },
                Job::AddMod(result) => match result {
                    Ok(Some(entry)) => self.edit_mods(move |p| mods::add(&mut p.mods, entry), cx),
                    Ok(None) => {}
                    Err(e) => self.message(e.to_string(), true),
                },
                Job::RefreshMods(result) => match result {
                    Ok(entries) => self.edit_mods(
                        move |p| {
                            p.mods = entries;
                            Ok(())
                        },
                        cx,
                    ),
                    Err(e) => self.message(e.to_string(), true),
                },
            }
            cx.notify();
        }
        if self.capture.is_some() && window.is_window_active() && !self.locked() {
            let pressed = capture::pressed();
            let (key, previous) = self.capture.as_ref().unwrap();
            let key = key.clone();
            let newly = pressed
                .iter()
                .find(|k| !previous.contains(k) && capture::code(**k).is_some())
                .copied();
            if let Some(vk) = newly {
                if vk == 27 {
                    self.capture = None;
                    self.message("Capture cancelled.", false);
                } else if let Some(code) = capture::physical_code(vk) {
                    match self.profile.rebind(&key, code) {
                        Ok(()) => {
                            self.capture = None;
                            self.sync(window, cx);
                            self.message("Binding captured. Save settings to apply.", false);
                        }
                        Err(e) => self.message(e.to_string(), true),
                    }
                }
                cx.notify();
            }
            if let Some((_, previous)) = &mut self.capture {
                *previous = pressed;
            }
        }
        if self.last_poll.elapsed() < Duration::from_millis(350) {
            return;
        }
        self.last_poll = Instant::now();
        if let Some(session) = &mut self.session {
            match session.poll() {
                Ok((changed, code)) => {
                    if changed {
                        self.log = session.last_log.clone().into();
                        cx.notify();
                    }
                    if let Some(code) = code {
                        self.session = None;
                        self.load(window, cx);
                        self.message(
                            format!("Session ended (exit {code}). Insert settings reloaded."),
                            code != 0,
                        );
                        if code != 0 {
                            self.navigate(4, cx);
                        }
                        cx.notify();
                        #[cfg(feature = "ui-validation")]
                        if let Some(case) = &self.smoke_case {
                            let summary = serde_json::json!({"exit_code":code,"settings_reloaded":self.profile.get("fg_multiplier")=="4","log_directory":self.log_dir});
                            let _ = model::atomic_write(
                                &case.join("ui-summary.json"),
                                &summary.to_string(),
                            );
                            cx.quit();
                        }
                    }
                }
                Err(e) => {
                    self.message(format!("Log read failed: {e}"), true);
                    cx.notify();
                }
            }
        }
    }
    fn switch(
        &self,
        key: &'static str,
        label: &'static str,
        description: &'static str,
        cx: &mut Context<Self>,
    ) -> Div {
        let checked = match key {
            "@use_patch" => self.prefs.use_patch,
            _ => self.profile.flag(key),
        };
        row(
            label,
            description,
            Switch::new(key)
                .checked(checked)
                .disabled(self.locked())
                .on_change(cx.listener(move |this, value, _, cx| {
                    if key == "@use_patch" {
                        this.prefs.use_patch = *value;
                    } else {
                        this.profile.set(key, if *value { "1" } else { "0" });
                    }
                    cx.notify();
                })),
        )
    }
    fn select(&self, key: &str) -> AnyElement {
        Select::new(&self.choices[key].state)
            .id(format!("select-{key}"))
            .disabled(self.locked())
            .w_full()
            .into_any_element()
    }
    fn input(&self, key: &str) -> AnyElement {
        Input::new(&self.inputs[key])
            .disabled(self.locked())
            .w_full()
            .into_any_element()
    }
    fn folder(&self, key: &'static str, label: &'static str, cx: &mut Context<Self>) -> Div {
        div()
            .flex()
            .flex_col()
            .gap_2()
            .child(div().text_sm().child(label))
            .child(
                div()
                    .flex()
                    .gap_2()
                    .items_center()
                    .child(div().flex_1().child(self.input(&format!("@{key}"))))
                    .child(
                        Button::new(format!("browse-{key}"))
                            .outline()
                            .icon(IconName::Folder)
                            .label("Browse")
                            .disabled(self.locked())
                            .on_click(
                                cx.listener(move |this, _, _, cx| this.browse(key.into(), cx)),
                            )
                            .animated(
                                format!("browse-motion-{key}"),
                                ButtonKind::Outline,
                                !(self.locked()),
                            ),
                    ),
            )
    }
    fn hero(&self, cx: &mut Context<Self>) -> Div {
        div()
            .relative()
            .h(px(248.))
            .w_full()
            .overflow_hidden()
            .rounded_xl()
            .border_1()
            .border_color(rgb(0x3b4147))
            .bg(rgb(0x151e27))
            .child(
                img("launcher/hunter-workshop.png")
                    .id("workshop-artwork")
                    .absolute()
                    .top_0()
                    .left_0()
                    .w_full()
                    // Explicit height avoids an intrinsic-image layout shift; the
                    // banner clips the lower half while retaining the hunter's head.
                    .h(px(512.))
                    .object_fit(ObjectFit::Cover),
            )
            .child(
                div()
                    .absolute()
                    .top_0()
                    .left_0()
                    .size_full()
                    .bg(linear_gradient(
                        90.,
                        linear_color_stop(rgba(0x10151bf5), 0.15),
                        linear_color_stop(rgba(0x10151b00), 0.75),
                    )),
            )
            .child(
                div()
                    .relative()
                    .size_full()
                    .p_7()
                    .flex()
                    .flex_col()
                    .justify_between()
                    .child(
                        div()
                            .flex()
                            .items_center()
                            .gap_2()
                            .child(div().w(px(6.)).h(px(6.)).rounded_full().bg(rgb(0xcbb082)))
                            .child(
                                div()
                                    .text_xs()
                                    .font_weight(FontWeight::MEDIUM)
                                    .text_color(rgb(0xd5bc92))
                                    .child("THE HUNTER'S WORKSHOP"),
                            ),
                    )
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .gap_2()
                            .child(
                                div()
                                    .font_family("Georgia")
                                    .text_size(px(38.))
                                    .text_color(rgb(0xf0ebe2))
                                    .child("The night awaits."),
                            )
                            .child(
                                div()
                                    .text_sm()
                                    .text_color(rgb(0xb2b8bd))
                                    .child("Return to Yharnam. Make the hunt your own."),
                            ),
                    )
                    .child(
                        div()
                            .flex()
                            .items_center()
                            .gap_3()
                            .child(
                                Button::new("hero-launch")
                                    .primary()
                                    .icon(IconName::Play)
                                    .label("Launch game")
                                    .disabled(self.locked())
                                    .on_click(
                                        cx.listener(|this, _, window, cx| this.launch(window, cx)),
                                    )
                                    .animated(
                                        "button-motion-1",
                                        ButtonKind::Primary,
                                        !(self.locked()),
                                    ),
                            )
                            .child(
                                div()
                                    .text_xs()
                                    .text_color(rgb(0xb2b8bd))
                                    .child(self.game_info.clone()),
                            ),
                    ),
            )
    }
    fn overview(&self, cx: &mut Context<Self>) -> Div {
        let upscaler = self.choices["upscaler"]
            .options
            .iter()
            .find(|(_, value)| value == self.profile.get("upscaler"))
            .map(|(label, _)| label.as_str())
            .unwrap_or("Custom");
        let resolution = self.profile.get("output_res").replace('x', " × ");
        let fg = if self.profile.flag("frame_generation") {
            format!("{}X generation", self.profile.get("fg_multiplier"))
        } else {
            "Disabled".into()
        };
        div()
            .flex()
            .gap_3()
            .children(
                [
                    (IconName::Monitor, "OUTPUT", resolution),
                    (IconName::Sparkles, "RECONSTRUCTION", upscaler.to_owned()),
                    (IconName::Layers, "FRAME GENERATION", fg),
                ]
                .into_iter()
                .enumerate()
                .map(|(index, (icon, label, value))| {
                    let inner = div()
                        .flex_1()
                        .min_w_0()
                        .p_4()
                        .rounded_lg()
                        .border_1()
                        .border_color(rgb(0x2a3038))
                        .bg(rgb(0x171c23))
                        .flex()
                        .items_center()
                        .gap_3()
                        .child(
                            div()
                                .w(px(34.))
                                .h(px(34.))
                                .flex_shrink_0()
                                .rounded_lg()
                                .bg(rgb(0x242a30))
                                .flex()
                                .items_center()
                                .justify_center()
                                .child(Icon::new(icon).size(px(16.)).text_color(rgb(0xcbb082))),
                        )
                        .child(
                            div()
                                .min_w_0()
                                .flex()
                                .flex_col()
                                .gap_1()
                                .child(
                                    div()
                                        .text_size(px(10.))
                                        .text_color(rgb(0xa6adb5))
                                        .child(label),
                                )
                                .child(
                                    div()
                                        .text_sm()
                                        .font_weight(FontWeight::MEDIUM)
                                        .truncate()
                                        .child(value),
                                ),
                        );
                    Surface::new(format!("summary-{label}"), inner, index as u64 * 30)
                }),
            )
            .child(
                Button::new("overview-graphics")
                    .ghost()
                    .icon(IconName::Settings)
                    .label("Edit")
                    .on_click(cx.listener(|this, _, _, cx| {
                        this.navigate(1, cx);
                        cx.notify();
                    }))
                    .animated("button-motion-2", ButtonKind::Ghost, true),
            )
    }
    fn play(&self, cx: &mut Context<Self>) -> Div {
        div()
            .flex()
            .flex_col()
            .gap_5()
            .child(self.hero(cx))
            .child(self.overview(cx))
            .child(
                card(
                    "Game files",
                    "Use your merged 01.09 dump, or choose a matching separate update.",
                )
                .child(self.folder("game", "Game folder", cx))
                .child(self.switch(
                    "@use_patch",
                    "Use a separate update",
                    "Leave off for a merged 01.09 game.",
                    cx,
                ))
                .when(self.prefs.use_patch, |d| {
                    d.child(self.folder("patch", "Update folder", cx))
                }),
            )
            .child(
                card(
                    "Saves & profile",
                    "The user folder contains savedata and shader cache.",
                )
                .child(self.folder("user", "Port user folder", cx))
                .child(
                    div()
                        .flex()
                        .gap_2()
                        .child(
                            Button::new("import")
                                .outline()
                                .icon(IconName::Download)
                                .label("Import shadPS4 save")
                                .disabled(self.locked())
                                .on_click(cx.listener(|this, _, _, cx| this.import(cx)))
                                .animated("button-motion-3", ButtonKind::Outline, !(self.locked())),
                        )
                        .child(
                            Button::new("open-user")
                                .ghost()
                                .label("Open folder")
                                .on_click(
                                    cx.listener(|this, _, _, cx| this.open_folder("user", cx)),
                                )
                                .animated("button-motion-4", ButtonKind::Ghost, true),
                        ),
                )
                .child(self.folder("output", "Settings / output folder", cx))
                .child(
                    Button::new("load")
                        .outline()
                        .label("Load selected profile")
                        .disabled(self.locked())
                        .on_click(cx.listener(|this, _, window, cx| this.reload(window, cx)))
                        .animated("button-motion-5", ButtonKind::Outline, !(self.locked())),
                ),
            )
            .child(
                card(
                    "Runtime",
                    "Launch an existing build. Game preparation still uses Python 3.",
                )
                .child(self.folder("build", "Executable folder", cx))
                .child(
                    div()
                        .flex()
                        .flex_col()
                        .gap_2()
                        .child(div().text_sm().child("Custom launch options"))
                        .child({
                            let field = div().id("launch-options-field").child(
                                Input::new(&self.inputs["@launch_options"])
                                    .disabled(self.locked())
                                    .w_full(),
                            );
                            #[cfg(feature = "ui-validation")]
                            let field = field.test_support();
                            field
                        })
                        .child(
                            div().text_xs().text_color(rgb(0x96959e)).child(
                                "New memory model is the default. Options here override launch settings; leave empty for defaults.",
                            ),
                        ),
                )
                .child(
                    div()
                        .text_sm()
                        .text_color(rgb(0x96959e))
                        .child("Insert opens game settings · F8 releases the mouse"),
                ),
            )
    }
    fn graphics(&self, cx: &mut Context<Self>) -> Div {
        div()
            .flex()
            .flex_col()
            .gap_5()
            .child(
                card("Display", "Output and window resolution are independent.")
                    .child(row(
                        "Output resolution",
                        "Final reconstructed image.",
                        self.select("output_res"),
                    ))
                    .child(row(
                        "Window resolution",
                        "Window / swapchain size.",
                        self.select("@window"),
                    ))
                    .child(row(
                        "Resolution changes",
                        "Startup patches or live host scaling.",
                        self.select("live_resolution"),
                    ))
                    .child(row(
                        "Window mode",
                        "Shared with Insert; F11 toggles fullscreen while playing.",
                        self.select("window_mode"),
                    )),
            )
            .child(
                card("Reconstruction", "Choose image quality and sharpening.")
                    .child(row(
                        "Upscaler",
                        "DLSS, FSR or native rendering.",
                        self.select("upscaler"),
                    ))
                    .child(row(
                        "Preset",
                        "Internal reconstruction quality.",
                        self.select("preset"),
                    ))
                    .child(self.switch("sharpen", "Image sharpening", "RCAS sharpening.", cx))
                    .child(row("Sharpness", "0.00–2.00", self.input("sharpness")))
                    .child(self.switch("jitter", "Subpixel jitter", "Temporal sampling.", cx))
                    .child(self.switch(
                        "object_motion",
                        "Character motion vectors",
                        "Moving-object reconstruction.",
                        cx,
                    )),
            )
            .child(
                card(
                    "Frame generation",
                    "Experimental. Actual capability limits are enforced in game.",
                )
                .child(row(
                    "FG runtime",
                    "Select the matching executable deployment.",
                    self.select("@engine"),
                ))
                .child(self.switch(
                    "frame_generation",
                    "Enable Frame Generation",
                    "Generated FPS is separate from rendered FPS.",
                    cx,
                ))
                .child(row(
                    "Multiplier",
                    "2X–6X, depending on the runtime.",
                    self.select("fg_multiplier"),
                ))
                .child(row(
                    "NVIDIA Reflex",
                    "Independent of FG. Requires the prepared NVIDIA runtime.",
                    self.select("reflex"),
                )),
            )
            .child(
                card("Frame pacing", "Immediate allows tearing; FIFO uses VSync.")
                    .child(row(
                        "Presentation",
                        "FG currently requires Immediate.",
                        self.select("@present"),
                    ))
                    .child(row(
                        "Game FPS patch",
                        "Runtime memory patch.",
                        self.select("@fps_patch"),
                    ))
                    .child(row(
                        "FPS limit",
                        "0 = unlimited; maximum 480.",
                        self.input("@fps_limit"),
                    ))
                    .child(self.switch(
                        "show_fps",
                        "Show FPS counter",
                        "Rendered / presented frame statistics.",
                        cx,
                    )),
            )
    }
    fn effects(&self, cx: &mut Context<Self>) -> Div {
        let mut effects = card(
            "Rendering effects",
            "Applied at the next launch. Insert can apply and restart during a session.",
        );
        for &(key, label, _) in model::EFFECTS {
            if key == "vertex_explosion_fix" {
                continue;
            }
            effects = effects.child(self.switch(key, label, "", cx));
        }
        div().flex().flex_col().gap_5().child(
            card("Vertex explosion fix", "Optional workaround for stretched polygons caused by FaceGen.")
                .child(
                    Checkbox::new("vertex_explosion_fix")
                        .label("Disable FaceGen")
                        .checked(self.profile.flag("vertex_explosion_fix"))
                        .disabled(self.locked())
                        .on_change(cx.listener(|this, enabled, _, cx| {
                            this.profile.set("vertex_explosion_fix", if *enabled { "1" } else { "0" });
                            cx.notify();
                        })),
                )
                .child(div().text_xs().text_color(rgb(0xa4a8ae)).child(
                    "Disables custom face shapes for the player and affected NPCs. Requires a game restart. Does not cover vertex corruption from other causes.",
                )),
        ).child(effects).child(
            card("Model detail", "Control the game's model LOD bias.").child(row(
                "Detail",
                "Game default is neutral.",
                self.select("model_lod"),
            )),
        )
    }
    fn controls(&self, cx: &mut Context<Self>) -> Div {
        let selected = model::bindings()
            .into_iter()
            .find(|b| b.key == self.selected);
        let mut table = card(
            "Controller bindings",
            "Primary and secondary inputs share the same runtime mapping as Insert.",
        );
        let mut actions = model::bindings();
        actions.sort_by_key(|b| b.key != self.selected);
        table = table.child(
            div()
                .flex()
                .gap_3()
                .text_xs()
                .text_color(rgb(0x96959e))
                .child(div().w(px(175.)).child("CONTROLLER / ACTION"))
                .child(div().flex_1().child("PRIMARY"))
                .child(div().flex_1().child("SECONDARY")),
        );
        for b in actions {
            let key = format!("bind_{}", b.key);
            let alt = format!("{key}_alt");
            let action = b.key.clone();
            let primary_capture = {
                let key = key.clone();
                Button::new(format!("capture-{key}")).ghost().icon(IconName::Keyboard).small().label("Capture")
                    .disabled(self.locked()).on_click(cx.listener(move |this,_,_,cx|{
                        this.capture=Some((key.clone(),capture::pressed()));this.message("Press a key or mouse button. Escape cancels; Insert, F8 and F11 are reserved.",false);cx.notify();
                    })).animated("button-motion-6", ButtonKind::Ghost, !(self.locked())).into_any_element()
            };
            table = table.child(
                div()
                    .id(format!("binding-row-{}", b.key))
                    .flex()
                    .items_center()
                    .gap_3()
                    .py_2()
                    .border_b_1()
                    .border_color(rgb(0x272b32))
                    .text_color(if b.key == self.selected {
                        rgb(0xd4b47b)
                    } else {
                        rgb(0xe8e8ec)
                    })
                    .child(
                        div()
                            .w(px(175.))
                            .flex_shrink_0()
                            .text_sm()
                            .child(
                                Button::new(format!("action-{}", b.key))
                                    .ghost()
                                    .label(b.label.clone())
                                    .small()
                                    .on_click(cx.listener(move |this, _, _, cx| {
                                        this.select_control(action.clone(), cx);
                                    }))
                                    .animated("button-motion-7", ButtonKind::Ghost, true),
                            )
                            .child(div().text_xs().text_color(rgb(0x96959e)).child(b.action)),
                    )
                    .child(
                        div()
                            .flex_1()
                            .flex()
                            .flex_col()
                            .gap_1()
                            .child(self.select(&key))
                            .child(primary_capture),
                    )
                    .child(
                        div()
                            .flex_1()
                            .flex()
                            .flex_col()
                            .gap_1()
                            .child(self.select(&alt))
                            .child(
                                Button::new(format!("capture-alt-{alt}"))
                                    .ghost()
                                    .icon(IconName::Keyboard)
                                    .small()
                                    .label("Capture")
                                    .disabled(self.locked())
                                    .on_click(cx.listener(move |this, _, _, cx| {
                                        this.capture = Some((alt.clone(), capture::pressed()));
                                        this.message("Press an input. Escape cancels.", false);
                                        cx.notify();
                                    }))
                                    .animated(
                                        "button-motion-8",
                                        ButtonKind::Ghost,
                                        !(self.locked()),
                                    ),
                            ),
                    ),
            );
        }
        div()
            .flex()
            .flex_col()
            .gap_5()
            .child(
                card(
                    "Input preferences",
                    "WASD moves. Mouse and arrow keys turn the camera.",
                )
                .child(self.switch(
                    "keyboard_mouse",
                    "Enable keyboard and mouse",
                    "Works alongside a controller.",
                    cx,
                ))
                .child(self.switch(
                    "mouse_capture",
                    "Capture mouse",
                    "F8 releases the cursor.",
                    cx,
                ))
                .child(self.switch(
                    "mouse_invert_y",
                    "Invert mouse Y",
                    "Reverse vertical camera movement.",
                    cx,
                ))
                .child(row(
                    "Sensitivity",
                    "0.10–5.00",
                    self.input("mouse_sensitivity"),
                ))
                .child(self.switch(
                    "dpad_camera",
                    "D-pad controls camera",
                    "Keep OFF for item / menu actions.",
                    cx,
                )),
            )
            .child(
                card(
                    "Your controller",
                    "Click a control or its L1/L2/R1/R2 label, then edit the binding below.",
                )
                .child(self.controller(cx))
                .child(
                    div()
                        .text_color(if selected.is_some() {
                            rgb(0xd4b47b)
                        } else {
                            rgb(0x96959e)
                        })
                        .text_sm()
                        .child(
                            selected
                                .map(|binding| format!("{} · {}", binding.label, binding.action))
                                .unwrap_or_else(|| "No control selected".into()),
                        ),
                ),
            )
            .child(table)
            .child(
                Button::new("reset-bindings")
                    .outline()
                    .label("Reset bindings to defaults")
                    .disabled(self.locked())
                    .on_click(cx.listener(|this, _, window, cx| {
                        for (k, v) in model::defaults() {
                            if k.starts_with("bind_") {
                                this.profile.set(&k, v);
                            }
                        }
                        this.sync(window, cx);
                        cx.notify();
                    }))
                    .animated("button-motion-9", ButtonKind::Outline, !(self.locked())),
            )
    }
    fn controller(&self, cx: &mut Context<Self>) -> Div {
        use crate::controller::{CONTROLS, HEIGHT, SHOULDER_LABELS, WIDTH};
        let mut body = div()
            .relative()
            .w_full()
            .aspect_ratio(WIDTH / HEIGHT)
            .rounded_lg()
            .overflow_hidden()
            .child(
                img("launcher/controller-dark.svg")
                    .id("controller-artwork")
                    .absolute()
                    .top_0()
                    .left_0()
                    .size_full()
                    .object_fit(ObjectFit::Contain),
            )
            .when(!self.selected.is_empty(), |body| {
                body.child(
                    img(format!("launcher/selection-{}.svg", self.selected))
                        .id("controller-selection")
                        .absolute()
                        .top_0()
                        .left_0()
                        .size_full()
                        .object_fit(ObjectFit::Contain)
                        .with_animation(
                            ("controller-choice", self.selected_revision),
                            Animation::new(Duration::from_millis(160))
                                .with_easing(ease_out_quint()),
                            |image, progress| image.opacity(0.45 + 0.55 * progress),
                        ),
                )
            });
        for control in CONTROLS {
            body = body.child(self.controller_target(
                control.key,
                control.label,
                control.bounds,
                false,
                cx,
            ));
        }
        for &(key, bounds) in SHOULDER_LABELS {
            let control = CONTROLS
                .iter()
                .find(|control| control.key == key)
                .expect("shoulder control");
            body = body.child(self.controller_target(key, control.label, bounds, true, cx));
        }
        body
    }
    fn controller_target(
        &self,
        key: &'static str,
        label: &'static str,
        [x, y, width, height]: [f32; 4],
        printed_label: bool,
        cx: &mut Context<Self>,
    ) -> Div {
        use crate::controller::{HEIGHT, WIDTH};
        // These transparent buttons provide semantics and interaction only.
        // The SVG contour follows the artwork independently of target padding.
        div()
            .absolute()
            .left(relative((x - width / 2.) / WIDTH))
            .top(relative((y - height / 2.) / HEIGHT))
            .w(relative(width / WIDTH))
            .h(relative(height / HEIGHT))
            .child(
                Button::new(if printed_label {
                    format!("diagram-label-{key}")
                } else {
                    format!("diagram-{key}")
                })
                .custom(
                    ButtonCustomVariant::new(cx)
                        .hover(hsla(0., 0., 0., 0.))
                        .active(hsla(0., 0., 0., 0.)),
                )
                .size_full()
                .p_0()
                .border_0()
                .focus_ring(false)
                .selected(self.selected == key)
                .cursor_pointer()
                .bg(hsla(0., 0., 0., 0.))
                .border_color(hsla(0., 0., 0., 0.))
                .accessibility_label(label)
                .on_click(cx.listener(move |this, _, _, cx| {
                    this.select_control(key.into(), cx);
                })),
            )
    }
    fn logs(&self, cx: &mut Context<Self>) -> Div {
        let text = self
            .log
            .lines()
            .rev()
            .take(350)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect::<Vec<_>>()
            .join("\n");
        div().flex().flex_col().gap_4()
            .child(card("Session output","Save from the game menu before Stop session; Stop exits without an additional save.")
                .child(div().flex().gap_2()
                    .child(Button::new("log-folder").outline().icon(IconName::Folder).label("Open log folder").on_click(cx.listener(|this,_,_,cx|{
                        if let Some(path)=&this.log_dir{let _=open_directory(path);}else{this.open_folder("output",cx);}
                    })).animated("button-motion-11", ButtonKind::Outline, true))
                    .child(Button::new("copy-log").ghost().label("Copy log").on_click(cx.listener(|this,_,_,cx|cx.write_to_clipboard(ClipboardItem::new_string(this.log.to_string())))).animated("button-motion-12", ButtonKind::Ghost, true)))
                .child(div().text_xs().text_color(rgb(0x96959e)).child(self.log_dir.as_ref().map(|p|p.display().to_string()).unwrap_or_else(||"No session yet".into()))))
            .child(div().id("log-scroll").h(px(480.)).overflow_y_scroll().rounded_lg().bg(rgb(0x090b0f)).border_1().border_color(rgb(0x272b32))
                .p_4().font_family("Consolas").text_xs().text_color(rgb(0xb6bdc9)).child(text))
    }
    fn edit_mods(
        &mut self,
        edit: impl FnOnce(&mut Preferences) -> Result<()>,
        cx: &mut Context<Self>,
    ) {
        if self.locked() {
            return;
        }
        let before = self.prefs.clone();
        let result = edit(&mut self.prefs).and_then(|_| {
            #[cfg(feature = "ui-validation")]
            if let Some(path) = &self.smoke_mod_profile {
                return model::atomic_write(
                    path,
                    &(serde_json::to_string_pretty(&self.prefs)? + "\n"),
                );
            }
            self.prefs.save(&self.root)
        });
        match result {
            Ok(()) => self.message("Mod selection saved. Applies on the next launch.", false),
            Err(e) => {
                self.prefs = before;
                self.message(e.to_string(), true);
            }
        }
        cx.notify();
    }
    fn add_mod(&mut self, cx: &mut Context<Self>) {
        if self.locked() {
            return;
        }
        self.busy = true;
        let sender = self.sender.clone();
        std::thread::spawn(move || {
            let result = rfd::FileDialog::new()
                .set_title("Select extracted Bloodborne mod")
                .pick_folder()
                .map(|path| mods::inspect(&path))
                .transpose();
            let _ = sender.send(Job::AddMod(result));
        });
        cx.notify();
    }
    fn refresh_mods(&mut self, cx: &mut Context<Self>) {
        if self.locked() {
            return;
        }
        self.busy = true;
        let entries = self.prefs.mods.clone();
        let sender = self.sender.clone();
        std::thread::spawn(move || {
            let result = entries
                .into_iter()
                .map(|old| {
                    let mut entry = mods::inspect(Path::new(&old.path))?;
                    entry.enabled = old.enabled;
                    entry.name = old.name;
                    Ok(entry)
                })
                .collect();
            let _ = sender.send(Job::RefreshMods(result));
        });
        cx.notify();
    }
    fn mods_page(&self, cx: &mut Context<Self>) -> Div {
        let locked = self.locked();
        let active = self.prefs.mods.iter().filter(|m| m.enabled).count();
        let header = card("Mod library", "Add an extracted mod folder. Your original game files stay intact.")
            .child(div().flex().items_center().justify_between().gap_5().py_2()
                .child(div().flex_1().min_w_0().flex().flex_col().gap_1()
                    .child(div().text_sm().child("Enable mods"))
                    .child(div().text_xs().text_color(rgb(0xa6adb5))
                        .child("Switch off to launch the original game assets.")))
                .child(div().flex().items_center().justify_end().flex_shrink_0().gap_3()
                    .child(div().w(px(22.)).text_xs()
                        .text_color(if self.prefs.mods_enabled { rgb(0xd4b888) } else { rgb(0xa6adb5) })
                        .child(if self.prefs.mods_enabled { "On" } else { "Off" }))
                    .child(Switch::new("mods-enabled").checked(self.prefs.mods_enabled).disabled(locked)
                        .on_change(cx.listener(|this, value, _, cx| {
                            let enabled = *value;
                            this.edit_mods(move |p| { p.mods_enabled = enabled; Ok(()) }, cx);
                        })))))
            .child(div().flex().items_center().justify_between().gap_3()
                .child(div().text_xs().text_color(rgb(0xa6adb5)).child(format!("{} mods / {} active", self.prefs.mods.len(),
                    if self.prefs.mods_enabled { active } else { 0 })))
                .child(div().flex().gap_2()
                    .child(Button::new("mods-refresh").outline().icon(IconName::RotateCcw).label("Refresh")
                        .disabled(locked || self.prefs.mods.is_empty())
                        .on_click(cx.listener(|this, _, _, cx| this.refresh_mods(cx)))
                        .animated("mods-refresh-motion", ButtonKind::Outline, !locked))
                    .child(Button::new("mods-add").primary().label("Add mod")
                        .disabled(locked).on_click(cx.listener(|this, _, _, cx| this.add_mod(cx)))
                        .animated("mods-add-motion", ButtonKind::Primary, !locked))))
            .child(div().text_xs().text_color(rgb(0xa6adb5)).child(
                "dvdroot_ps4, direct asset folders (chr, map, param...) and extracted wrapper folders are detected automatically."));
        let mut library = div().flex().flex_col().gap_3();
        if self.prefs.mods.is_empty() {
            library = library.child(
                div()
                    .p_8()
                    .rounded_xl()
                    .border_1()
                    .border_color(rgb(0x303740))
                    .bg(rgb(0x151a21))
                    .flex()
                    .flex_col()
                    .items_center()
                    .gap_3()
                    .child(puzzle().size(px(32.)).text_color(rgb(0xcbb082)))
                    .child(div().text_lg().child("Make the hunt your own"))
                    .child(
                        div()
                            .text_sm()
                            .text_color(rgb(0xa6adb5))
                            .child("Add your first mod to get started."),
                    ),
            );
        }
        for (index, entry) in self.prefs.mods.iter().enumerate() {
            let running = self.prefs.mods_enabled && entry.enabled;
            let missing = !Path::new(&entry.path).is_dir();
            let mut actions = div().flex().items_center().gap_2();
            for (direction, label) in [(-1isize, "Move up"), (1, "Move down")] {
                let key = if direction == -1 { "up" } else { "down" };
                actions = actions.child(
                    Button::new(format!("mod-{key}-{index}"))
                        .ghost()
                        .label(label)
                        .disabled(
                            locked
                                || (direction == -1 && index == 0)
                                || (direction == 1 && index + 1 == self.prefs.mods.len()),
                        )
                        .on_click(cx.listener(move |this, _, _, cx| {
                            this.edit_mods(
                                move |p| {
                                    mods::move_entry(&mut p.mods, index, direction);
                                    Ok(())
                                },
                                cx,
                            )
                        }))
                        .animated(
                            format!("mod-{key}-motion-{index}"),
                            ButtonKind::Ghost,
                            !locked,
                        ),
                );
            }
            let path = PathBuf::from(&entry.path);
            actions = actions
                .child(
                    Button::new(format!("mod-open-{index}"))
                        .ghost()
                        .icon(IconName::Folder)
                        .label("Open folder")
                        .disabled(missing)
                        .on_click(cx.listener(move |this, _, _, cx| {
                            if let Err(e) = open_directory(&path) {
                                this.message(e.to_string(), true);
                                cx.notify();
                            }
                        }))
                        .animated(
                            format!("mod-open-motion-{index}"),
                            ButtonKind::Ghost,
                            !missing,
                        ),
                )
                .child(
                    Button::new(format!("mod-remove-{index}"))
                        .ghost()
                        .label("Remove")
                        .disabled(locked)
                        .on_click(cx.listener(move |this, _, _, cx| {
                            this.edit_mods(
                                move |p| {
                                    p.mods.remove(index);
                                    Ok(())
                                },
                                cx,
                            )
                        }))
                        .animated(
                            format!("mod-remove-motion-{index}"),
                            ButtonKind::Ghost,
                            !locked,
                        ),
                );
            let inner = div()
                .p_4()
                .rounded_xl()
                .border_1()
                .border_color(if running {
                    rgb(0x5a4d38)
                } else {
                    rgb(0x303740)
                })
                .bg(rgb(0x171c23))
                .flex()
                .flex_col()
                .gap_3()
                .child(
                    div()
                        .flex()
                        .items_center()
                        .gap_3()
                        .child(
                            div()
                                .size(px(38.))
                                .flex_shrink_0()
                                .rounded_lg()
                                .bg(rgb(0x272921))
                                .flex()
                                .items_center()
                                .justify_center()
                                .child(puzzle().size(px(19.)).text_color(rgb(0xd4b888))),
                        )
                        .child(
                            div()
                                .flex_1()
                                .min_w_0()
                                .flex()
                                .flex_col()
                                .gap_1()
                                .child(
                                    div()
                                        .text_sm()
                                        .font_weight(FontWeight::SEMIBOLD)
                                        .child(entry.name.clone()),
                                )
                                .child(div().text_xs().text_color(rgb(0xa6adb5)).child(format!(
                                    "#{} / {} files / {}",
                                    index + 1,
                                    entry.files,
                                    entry.layout
                                ))),
                        )
                        .child(
                            div()
                                .text_xs()
                                .text_color(if running {
                                    rgb(0xd4b888)
                                } else {
                                    rgb(0xa6adb5)
                                })
                                .child(if missing {
                                    "Folder missing"
                                } else if running {
                                    "On"
                                } else {
                                    "Off"
                                }),
                        )
                        .child(
                            Switch::new(format!("mod-toggle-{index}"))
                                .checked(entry.enabled)
                                .disabled(locked)
                                .on_change(cx.listener(move |this, value, _, cx| {
                                    let enabled = *value;
                                    this.edit_mods(
                                        move |p| {
                                            p.mods[index].enabled = enabled;
                                            Ok(())
                                        },
                                        cx,
                                    );
                                })),
                        ),
                )
                .child(
                    div()
                        .text_xs()
                        .text_color(rgb(0xa6adb5))
                        .child(entry.folders.join(" / ")),
                )
                .child(
                    div()
                        .text_xs()
                        .text_color(rgb(0x929ba5))
                        .child(entry.path.clone()),
                )
                .child(
                    div()
                        .flex()
                        .justify_end()
                        .border_t_1()
                        .border_color(rgb(0x303740))
                        .pt_2()
                        .child(actions),
                );
            library = library.child(Surface::new(
                format!("mod-card-{index}-{}", entry.path),
                inner,
                (index.min(2) * 25) as u64,
            ));
        }
        div().flex().flex_col().gap_4().child(header)
            .child(div().text_xs().text_color(rgb(0xb9a487)).child(
                "Changes are saved automatically and apply on the next launch. Lower mods win file overlaps. Remove only removes the list entry."))
            .child(library)
    }
    fn open_folder(&mut self, key: &str, cx: &mut Context<Self>) {
        let result = model::resolve(&self.root, self.prefs.folder(key)).and_then(|p| {
            fs::create_dir_all(&p)?;
            open_directory(&p)
        });
        if let Err(e) = result {
            self.message(e.to_string(), true);
            cx.notify();
        }
    }
    fn stop_session(&mut self, cx: &mut Context<Self>) {
        if let Some(s) = &mut self.session {
            match s.stop() {
                Ok(()) => self.message("Stop requested. Waiting for the runner…", false),
                Err(e) => self.message(e.to_string(), true),
            }
        }
        cx.notify();
    }
}
fn open_directory(path: &Path) -> Result<()> {
    std::process::Command::new("explorer.exe")
        .arg(path)
        .spawn()?;
    Ok(())
}
fn puzzle() -> Icon {
    Icon::new(IconName::Layers).path("launcher/puzzle.svg")
}
fn pairs(v: &[(&str, &str)]) -> Vec<(String, String)> {
    v.iter()
        .map(|(a, b)| (a.to_string(), b.to_string()))
        .collect()
}
fn same(v: &[&str]) -> Vec<(String, String)> {
    v.iter().map(|v| (v.to_string(), v.to_string())).collect()
}
fn card(title: &'static str, description: &'static str) -> Surface {
    let icon = match title {
        "Game files" | "Runtime" => IconName::Folder,
        "Saves & profile" => IconName::Download,
        "Display" => IconName::Monitor,
        "Reconstruction" => IconName::Sparkles,
        "Frame generation" => IconName::Layers,
        "Vertex explosion fix" => IconName::Layers,
        "Frame pacing" => IconName::Gauge,
        "Rendering effects" | "Model detail" => IconName::Sun,
        "Session output" => IconName::FileText,
        _ => IconName::Keyboard,
    };
    let inner = div()
        .flex()
        .flex_col()
        .gap_4()
        .p_5()
        .rounded_xl()
        .border_1()
        .border_color(rgb(0x2a3038))
        .bg(rgb(0x171c23))
        .child(
            div()
                .flex()
                .flex_col()
                .gap_2()
                .pb_3()
                .border_b_1()
                .border_color(rgb(0x303740))
                .child(
                    div()
                        .flex()
                        .items_center()
                        .gap_3()
                        .child(
                            div()
                                .w(px(32.))
                                .h(px(32.))
                                .flex_shrink_0()
                                .rounded_lg()
                                .border_1()
                                .border_color(rgb(0x504633))
                                .bg(linear_gradient(
                                    135.,
                                    linear_color_stop(rgb(0x302a21), 0.),
                                    linear_color_stop(rgb(0x20252b), 1.),
                                ))
                                .flex()
                                .items_center()
                                .justify_center()
                                .child(
                                    if title == "Mod library" {
                                        puzzle()
                                    } else {
                                        Icon::new(icon)
                                    }
                                    .size(px(16.))
                                    .text_color(rgb(0xd4b888)),
                                ),
                        )
                        .child(
                            div()
                                .text_lg()
                                .font_weight(FontWeight::SEMIBOLD)
                                .child(title),
                        ),
                )
                .child(div().text_sm().text_color(rgb(0xa6adb5)).child(description)),
        );
    Surface::new(
        format!("card-{title}"),
        inner,
        match title {
            "Game files" | "Display" | "Input preferences" => 0,
            "Saves & profile" | "Reconstruction" | "Your controller" => 25,
            _ => 50,
        },
    )
}

fn row(label: &'static str, description: &'static str, control: impl IntoElement) -> Div {
    div()
        .flex()
        .items_center()
        .justify_between()
        .gap_5()
        .py_2()
        .child(
            div()
                .flex_1()
                .flex()
                .flex_col()
                .gap_1()
                .child(div().text_sm().child(label))
                .when(!description.is_empty(), |d| {
                    d.child(div().text_xs().text_color(rgb(0xa6adb5)).child(description))
                }),
        )
        .child(div().w(px(270.)).flex_shrink_0().child(control))
}
fn key_hint(key: &'static str, label: &'static str) -> Div {
    div()
        .flex()
        .items_center()
        .justify_between()
        .gap_2()
        .child(div().text_xs().text_color(rgb(0xa6adb5)).child(label))
        .child(
            div()
                .px_2()
                .py_1()
                .rounded_md()
                .border_1()
                .border_color(rgb(0x373c43))
                .bg(rgb(0x1e232a))
                .text_size(px(10.))
                .text_color(rgb(0xc6c9ce))
                .child(key),
        )
}
impl Render for Launcher {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let content = match self.page {
            0 => self.play(cx),
            1 => self.graphics(cx),
            2 => self.effects(cx),
            3 => self.controls(cx),
            5 => self.mods_page(cx),
            _ => self.logs(cx),
        };
        let nav_top = gpui_kit::base::spring(
            "navigation-position",
            px(self.page as f32 * 44.),
            cx.theme().motion_tokens().spring_move,
            window,
            cx,
        );
        let mut menu = div().relative().flex().flex_col().gap_0().child(
            div()
                .id("navigation-indicator")
                .absolute()
                .top(nav_top)
                .left_0()
                .w_full()
                .h(px(44.))
                .rounded_lg()
                .border_1()
                .border_color(rgb(0x5f513c))
                .bg(linear_gradient(
                    90.,
                    linear_color_stop(rgb(0x342e24), 0.),
                    linear_color_stop(rgb(0x24272b), 1.),
                ))
                .child(
                    div()
                        .absolute()
                        .left_0()
                        .top(px(12.))
                        .w(px(3.))
                        .h(px(20.))
                        .rounded_full()
                        .bg(rgb(0xd8bb89)),
                ),
        );
        for (index, (title, _)) in PAGES.iter().enumerate() {
            let active = self.page == index;
            let foreground = gpui_kit::base::transition(
                format!("navigation-foreground-{index}"),
                Hsla::from(if active { rgb(0xe8d2ac) } else { rgb(0xabb5c1) }),
                crate::motion::quick(),
                window,
                cx,
            );
            menu = menu.child(
                Button::new(format!("navigate-{index}"))
                    .ghost()
                    .w_full()
                    .h(px(44.))
                    .px_3()
                    .accessibility_label(*title)
                    .child(
                        div()
                            .w_full()
                            .flex()
                            .items_center()
                            .gap_3()
                            .text_color(foreground)
                            .child(
                                (if index == 5 {
                                    puzzle()
                                } else {
                                    Icon::new(match index {
                                        0 => IconName::Play,
                                        1 => IconName::Monitor,
                                        2 => IconName::Sun,
                                        3 => IconName::Keyboard,
                                        _ => IconName::FileText,
                                    })
                                })
                                .size(px(16.)),
                            )
                            .child(
                                div()
                                    .flex_1()
                                    .text_sm()
                                    .font_weight(if active {
                                        FontWeight::SEMIBOLD
                                    } else {
                                        FontWeight::NORMAL
                                    })
                                    .child(*title),
                            ),
                    )
                    .on_click(cx.listener(move |this, _, _, cx| this.navigate(index, cx)))
                    .animated(
                        format!("navigation-button-{index}"),
                        ButtonKind::Ghost,
                        true,
                    ),
            );
        }
        div()
            .track_focus(&self.focus)
            .flex()
            .size_full()
            .bg(rgb(0x101318))
            .text_color(rgb(0xe9e5df))
            .text_sm()
            .font_family("Segoe UI")
            .child(
                div()
                    .w(px(226.))
                    .flex_shrink_0()
                    .flex()
                    .flex_col()
                    .justify_between()
                    .border_r_1()
                    .border_color(rgb(0x2a3038))
                    .bg(rgb(0x13171d))
                    .child(
                        div()
                            .flex()
                            .flex_col()
                            .child(
                                div()
                                    .h(px(100.))
                                    .px_5()
                                    .flex()
                                    .items_center()
                                    .gap_3()
                                    .child(
                                        div()
                                            .w(px(36.))
                                            .h(px(42.))
                                            .flex_shrink_0()
                                            .rounded_lg()
                                            .overflow_hidden()
                                            .border_1()
                                            .border_color(rgb(0x5b4e3a))
                                            .child(
                                                img("launcher/brand-cover.jpg")
                                                    .id("brand-cover")
                                                    .size_full()
                                                    .object_fit(ObjectFit::Cover),
                                            ),
                                    )
                                    .child(
                                        div()
                                            .flex()
                                            .flex_col()
                                            .gap_1()
                                            .child(
                                                div()
                                                    .font_family("Georgia")
                                                    .text_size(px(16.))
                                                    .font_weight(FontWeight::SEMIBOLD)
                                                    .child("BLOODBORNE"),
                                            )
                                            .child(
                                                div()
                                                    .text_size(px(10.))
                                                    .text_color(rgb(0xb9a487))
                                                    .child("BBPORT  /  PC EDITION"),
                                            ),
                                    ),
                            )
                            .child(
                                div()
                                    .px_5()
                                    .pb_3()
                                    .text_size(px(10.))
                                    .text_color(rgb(0x929ba5))
                                    .child("YOUR WORKSHOP"),
                            )
                            .child(div().px_3().child(menu)),
                    )
                    .child(
                        div()
                            .p_4()
                            .flex()
                            .flex_col()
                            .gap_4()
                            .child(
                                div()
                                    .rounded_lg()
                                    .border_1()
                                    .border_color(rgb(0x2a3038))
                                    .bg(rgb(0x181e25))
                                    .p_3()
                                    .flex()
                                    .flex_col()
                                    .gap_3()
                                    .child(
                                        div()
                                            .text_size(px(10.))
                                            .text_color(rgb(0xb9a487))
                                            .child("IN THE HUNT"),
                                    )
                                    .child(key_hint("Insert", "Game settings"))
                                    .child(key_hint("F8", "Release mouse"))
                                    .child(key_hint("F11", "Fullscreen")),
                            )
                            .child(
                                div()
                                    .px_1()
                                    .flex()
                                    .justify_between()
                                    .text_size(px(10.))
                                    .text_color(rgb(0x929ba5))
                                    .child("WINDOWS / LOCAL")
                                    .child("bbport"),
                            ),
                    ),
            )
            .child(
                div()
                    .flex_1()
                    .min_w_0()
                    .flex()
                    .flex_col()
                    .child(
                        div()
                            .h(px(57.))
                            .flex_shrink_0()
                            .px_6()
                            .flex()
                            .items_center()
                            .justify_between()
                            .border_b_1()
                            .border_color(rgb(0x2a3038))
                            .child(
                                div()
                                    .flex()
                                    .items_center()
                                    .gap_3()
                                    .text_xs()
                                    .child(div().text_color(rgb(0x929ba5)).child("Workshop"))
                                    .child(div().text_color(rgb(0x626b76)).child("/"))
                                    .child(PAGES[self.page].0),
                            )
                            .child(
                                div()
                                    .flex()
                                    .items_center()
                                    .gap_2()
                                    .rounded_full()
                                    .border_1()
                                    .border_color(rgb(0x2a3038))
                                    .px_3()
                                    .py_1()
                                    .child(div().w(px(5.)).h(px(5.)).rounded_full().bg(
                                        if self.session.is_some() {
                                            rgb(0x91bd9f)
                                        } else {
                                            rgb(0x919aa4)
                                        },
                                    ))
                                    .child(
                                        div().text_size(px(10.)).text_color(rgb(0xb7bec7)).child(
                                            if self.session.is_some() {
                                                "SESSION RUNNING"
                                            } else {
                                                "NOT RUNNING"
                                            },
                                        ),
                                    ),
                            ),
                    )
                    .child(
                        div()
                            .id(("page", self.page))
                            .flex_1()
                            .min_h_0()
                            .min_w_0()
                            .overflow_y_scroll()
                            .track_scroll(&self.scroll[self.page])
                            .p_6()
                            .child(
                                div()
                                    .flex()
                                    .flex_col()
                                    .gap_5()
                                    .when(self.page != 0, |d| {
                                        d.child(
                                            div()
                                                .flex()
                                                .flex_col()
                                                .gap_2()
                                                .child(
                                                    div()
                                                        .text_size(px(28.))
                                                        .font_weight(FontWeight::SEMIBOLD)
                                                        .child(PAGES[self.page].0),
                                                )
                                                .child(
                                                    div()
                                                        .text_sm()
                                                        .text_color(rgb(0xa6adb5))
                                                        .child(PAGES[self.page].1),
                                                ),
                                        )
                                    })
                                    .when(self.capture.is_some(), |d| {
                                        d.child(
                                            div()
                                                .rounded_lg()
                                                .border_1()
                                                .border_color(rgb(0xcbb082))
                                                .bg(rgb(0x28251f))
                                                .p_4()
                                                .flex()
                                                .items_center()
                                                .justify_between()
                                                .child("Listening for an input… Escape cancels.")
                                                .child(
                                                    Button::new("cancel-capture")
                                                        .ghost()
                                                        .label("Cancel")
                                                        .on_click(cx.listener(|this, _, _, cx| {
                                                            this.capture = None;
                                                            cx.notify();
                                                        }))
                                                        .animated(
                                                            "button-motion-14",
                                                            ButtonKind::Ghost,
                                                            true,
                                                        ),
                                                ),
                                        )
                                    })
                                    .child(
                                        content.with_animation(
                                            ("page-entry", self.page_revision),
                                            Animation::new(Duration::from_millis(260))
                                                .with_easing(ease_out_quint()),
                                            |content, progress| {
                                                content
                                                    .relative()
                                                    .top(px(8. * (1. - progress)))
                                                    .opacity(0.65 + 0.35 * progress)
                                            },
                                        ),
                                    ),
                            ),
                    )
                    .child(
                        div()
                            .flex_shrink_0()
                            .flex()
                            .items_center()
                            .justify_between()
                            .gap_3()
                            .px_5()
                            .py_4()
                            .border_t_1()
                            .border_color(rgb(0x2a3038))
                            .bg(rgb(0x151a21))
                            .child(
                                div()
                                    .flex_1()
                                    .min_w_0()
                                    .flex()
                                    .flex_col()
                                    .gap_1()
                                    .child(
                                        div()
                                            .text_xs()
                                            .truncate()
                                            .text_color(if self.error {
                                                rgb(0xf08086)
                                            } else {
                                                rgb(0xb7bec7)
                                            })
                                            .child(self.status.clone()),
                                    )
                                    .child(
                                        div().text_size(px(10.)).text_color(rgb(0xcbb082)).child(
                                            if self.profile.dirty() {
                                                "Unsaved changes"
                                            } else {
                                                "Shared with in-game settings"
                                            },
                                        ),
                                    ),
                            )
                            .child(
                                Button::new("reload")
                                    .ghost()
                                    .icon(IconName::RotateCw)
                                    .label("Reload")
                                    .disabled(self.locked())
                                    .on_click(
                                        cx.listener(|this, _, window, cx| this.reload(window, cx)),
                                    )
                                    .animated(
                                        "button-motion-15",
                                        ButtonKind::Ghost,
                                        !(self.locked()),
                                    ),
                            )
                            .child(
                                Button::new("save")
                                    .outline()
                                    .label("Save settings")
                                    .disabled(self.locked())
                                    .on_click(cx.listener(|this, _, window, cx| {
                                        this.save_clicked(window, cx)
                                    }))
                                    .animated(
                                        "button-motion-16",
                                        ButtonKind::Outline,
                                        !(self.locked()),
                                    ),
                            )
                            .when(self.session.is_none(), |d| {
                                d.child(
                                    Button::new("launch")
                                        .primary()
                                        .icon(IconName::Play)
                                        .label("Launch game")
                                        .disabled(self.locked())
                                        .on_click(cx.listener(|this, _, window, cx| {
                                            this.launch(window, cx)
                                        }))
                                        .animated(
                                            "button-motion-17",
                                            ButtonKind::Primary,
                                            !(self.locked()),
                                        ),
                                )
                            })
                            .when(self.session.is_some(), |d| {
                                d.child(
                                    Button::new("stop")
                                        .danger()
                                        .label("Stop session")
                                        .disabled(self.session.as_ref().is_none_or(|s| s.stopped))
                                        .on_click(
                                            cx.listener(|this, _, _, cx| this.stop_session(cx)),
                                        )
                                        .animated(
                                            "button-motion-18",
                                            ButtonKind::Danger,
                                            !(self.session.as_ref().is_none_or(|s| s.stopped)),
                                        ),
                                )
                            }),
                    ),
            )
    }
}
