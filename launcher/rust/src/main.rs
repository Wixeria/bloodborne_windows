#![windows_subsystem = "windows"]
mod assets;
mod controller;
mod motion;
mod ui;
use gpui_kit::component::{Theme, ThemeMode};
use gpui_kit::*;
fn main() {
    let root = match bloodborne_launcher::model::find_root() {
        Ok(p) => p,
        Err(e) => {
            rfd::MessageDialog::new()
                .set_title("Bloodborne Launcher")
                .set_description(e.to_string())
                .show();
            return;
        }
    };
    let error_path = root.join("out/windows-launcher/rust-error.log");
    std::panic::set_hook(Box::new(move |error| {
        let test = std::env::args().any(|s| {
            s == "--ui-smoke" || s == "--game-smoke" || s == "--motion-smoke" || s == "--mods-smoke" || s == "--launch-options-smoke" || s == "--effects-smoke"
        });
        if test {
            use std::io::Write as _;
            if let Ok(mut log) = std::fs::OpenOptions::new().create(true).append(true).open(&error_path) {
                let _ = writeln!(log, "{error}");
            }
        } else {
            let _ = std::fs::write(&error_path, error.to_string());
            rfd::MessageDialog::new()
                .set_title("Launcher error")
                .set_description(format!("{error}\n\nDetails: {}", error_path.display()))
                .show();
        }
    }));
    gpui_kit::application()
        .with_assets(assets::LauncherAssets)
        .run(move |cx| {
            gpui_kit::init(cx);
            Theme::change(ThemeMode::Dark, None, cx);
            Theme::update(cx, |theme| {
                theme.font_family = "Segoe UI".into();
                theme.font_size = px(14.);
                theme.radius = px(7.);
                theme.radius_lg = px(12.);
                theme.motion.duration_fast = std::time::Duration::from_millis(140);
                theme.motion.duration_normal = std::time::Duration::from_millis(220);
                theme.motion.spring_move = gpui_kit::base::Spring::new(std::time::Duration::from_millis(260))
                    .with_damping(0.95).with_epsilon(0.1);
                let colors = &mut theme.colors;
                colors.background = rgb(0x101318).into();
                colors.foreground = rgb(0xe9e5df).into();
                colors.border = rgb(0x2a3038).into();
                colors.input = rgb(0x343b44).into();
                colors.muted_foreground = rgb(0xa4a8ae).into();
                colors.primary = rgb(0xcbb082).into();
                colors.primary_hover = rgb(0xdec49a).into();
                colors.primary_active = rgb(0xb49769).into();
                colors.primary_foreground = rgb(0x16191c).into();
                colors.button_primary = colors.primary;
                colors.button_primary_hover = colors.primary_hover;
                colors.button_primary_active = colors.primary_active;
                colors.button_primary_foreground = colors.primary_foreground;
                colors.ring = rgb(0xb49769).into();
                colors.caret = colors.primary;
                colors.sidebar_accent = rgb(0x2a2723).into();
                colors.sidebar_accent_foreground = rgb(0xe1caa5).into();
                colors.popover = rgb(0x1a1f26).into();
                colors.popover_foreground = colors.foreground;
                colors.list = colors.popover;
                colors.list_active = rgb(0x33302b).into();
                colors.list_active_border = rgb(0x695a43).into();
                colors.list_hover = rgb(0x282e36).into();
            });
            cx.on_window_closed(|cx, _| {
                if cx.windows().is_empty() {
                    cx.quit();
                }
            })
            .detach();
            let options = WindowOptions {
                window_bounds: Some(WindowBounds::centered(size(px(1180.), px(820.)), cx)),
                window_min_size: Some(size(px(1000.), px(680.))),
                titlebar: Some(TitlebarOptions {
                    title: Some("Bloodborne · bbport".into()),
                    ..Default::default()
                }),
                ..Default::default()
            };
            let (_handle, _view) = gpui_kit::open_window(options, cx, move |window, cx| {
                cx.new(|cx| ui::Launcher::new(root, window, cx).expect("Launcher settings"))
            })
            .expect("Open launcher window");
            #[cfg(feature = "ui-validation")]
            if std::env::args().any(|s| s == "--effects-smoke") {
                let view = _view.clone();
                cx.spawn(async move |cx| {
                    verify_effects(_handle, &view, cx).await;
                    cx.update(|cx| cx.quit());
                }).detach();
            }
            #[cfg(feature = "ui-validation")]
            if std::env::args().any(|s| s == "--launch-options-smoke") {
                let view = _view.clone();
                cx.spawn(async move |cx| {
                    verify_launch_options(_handle, &view, cx).await;
                    cx.update(|cx| cx.quit());
                }).detach();
            }
            #[cfg(feature = "ui-validation")]
            if std::env::args().any(|s| s == "--motion-smoke") {
                let view = _view.clone();
                cx.spawn(async move |cx| {
                    verify_motion(_handle, &view, cx).await;
                    cx.update(|cx| cx.quit());
                }).detach();
            }
            #[cfg(feature = "ui-validation")]
            if std::env::args().any(|s| s == "--mods-smoke") {
                let view = _view.clone();
                cx.spawn(async move |cx| { verify_mods(_handle, &view, cx).await; }).detach();
            }
            #[cfg(feature = "ui-validation")]
            if std::env::args().any(|s| s == "--ui-smoke") {
                let output = bloodborne_launcher::model::find_root()
                    .expect("root")
                    .join("out/windows-launcher/rust-ui-smoke");
                std::fs::create_dir_all(&output).expect("snapshot folder");
                let _view = _view.clone();
                cx.spawn(async move |cx| {
                    let mut controller_checks = 0;
                    for (page, offset, name) in [
                        (0, 0., "play-initial"),
                        (1, 0., "graphics"),
                        (2, 0., "effects"),
                        (3, 0., "input"),
                        (4, 0., "logs"),
                        (5, 0., "mods"),
                        (3, 550., "bindings"),
                        (1, 750., "fg"),
                        (3, 1150., "bindings-table"),
                    ] {
                        _handle
                            .update(cx, |_, window, cx| {
                                _view.update(cx, |view, cx| view.smoke_page(page, offset, cx));
                                window.refresh();
                            })
                            .expect("show page");
                        cx.background_executor()
                            .timer(std::time::Duration::from_millis(900))
                            .await;
                        _handle
                            .update(cx, |_, window, _| {
                                window.render_to_image().and_then(|image| {
                                    image
                                        .save(output.join(format!("{name}.png")))
                                        .map_err(Into::into)
                                })
                            })
                            .expect("window snapshot")
                            .expect("save snapshot");
                        if name == "bindings" {
                            _handle.update(cx, |_, _, cx| {
                                assert!( _view.read(cx).smoke_selection().is_empty(), "a control was selected before the first click");
                            }).expect("initial controller selection");
                            controller_checks += verify_controller_clicks(_handle, &_view, cx, &output, 1180).await;
                            _handle
                                .update(cx, |_, window, cx| {
                                    window.dispatch_event(
                                        PlatformInput::MouseMove(MouseMoveEvent {
                                            position: point(px(881.), px(243.)),
                                            ..Default::default()
                                        }),
                                        cx,
                                    );
                                })
                                .expect("hover controller shoulder");
                            cx.background_executor()
                                .timer(std::time::Duration::from_millis(1000))
                                .await;
                            _handle
                                .update(cx, |_, window, _| {
                                    window.render_to_image().and_then(|image| {
                                        image
                                            .save(output.join("controller-hover.png"))
                                            .map_err(Into::into)
                                    })
                                })
                                .expect("hover frame")
                                .expect("save hover");
                        }
                    }
                    _handle
                        .update(cx, |_, window, cx| {
                            window.resize(size(px(1000.), px(680.)));
                            _view.update(cx, |view, cx| view.smoke_page(3, 550., cx));
                            window.refresh();
                        })
                        .expect("minimum window");
                    cx.background_executor()
                        .timer(std::time::Duration::from_millis(900))
                        .await;
                    _handle
                        .update(cx, |_, window, _| {
                            window.render_to_image().and_then(|image| {
                                image
                                    .save(output.join("minimum-window.png"))
                                    .map_err(Into::into)
                            })
                        })
                        .expect("minimum snapshot")
                        .expect("save");
                    controller_checks += verify_controller_clicks(_handle, &_view, cx, &output, 1000).await;
                    _handle.update(cx, |_, window, cx| {
                        window.resize(size(px(1440.), px(900.)));
                        _view.update(cx, |view, cx| view.smoke_page(3, 550., cx));
                        window.refresh();
                    }).expect("wide controller window");
                    cx.background_executor().timer(std::time::Duration::from_millis(900)).await;
                    controller_checks += verify_controller_clicks(_handle, &_view, cx, &output, 1440).await;
                    std::fs::write(output.join("controller-clicks.json"), serde_json::json!({"success":true,"clicks":controller_checks,"widths":[1180,1000,1440]}).to_string()).expect("click summary");
                    _handle.update(cx, |_, window, _| window.resize(size(px(1000.), px(680.))))
                        .expect("restore minimum window");
                    for (page, name) in [(0, "minimum-play"), (1, "minimum-graphics")] {
                        _handle
                            .update(cx, |_, window, cx| {
                                _view.update(cx, |view, cx| view.smoke_page(page, 0., cx));
                                window.refresh();
                            })
                            .expect("minimum page");
                        cx.background_executor()
                            .timer(std::time::Duration::from_millis(900))
                            .await;
                        _handle
                            .update(cx, |_, window, _| {
                                window.render_to_image().and_then(|image| {
                                    image
                                        .save(output.join(format!("{name}.png")))
                                        .map_err(Into::into)
                                })
                            })
                            .expect("minimum frame")
                            .expect("save");
                    }
                    // Capture the home artwork again after async image decoding has settled.
                    _handle
                        .update(cx, |_, window, cx| {
                            window.resize(size(px(1180.), px(820.)));
                            _view.update(cx, |view, cx| view.smoke_page(0, 0., cx));
                            window.refresh();
                        })
                        .expect("final home page");
                    cx.background_executor()
                        .timer(std::time::Duration::from_millis(900))
                        .await;
                    _handle
                        .update(cx, |_, window, _| {
                            window.render_to_image().and_then(|image| {
                                image.save(output.join("play.png")).map_err(Into::into)
                            })
                        })
                        .expect("home frame")
                        .expect("save");
                    cx.update(|cx| cx.quit());
                })
                .detach();
            }
            #[cfg(feature = "ui-validation")]
            if let Some(case) = std::env::args().skip_while(|s| s != "--game-smoke").nth(1) {
                _handle
                    .update(cx, |_, window, cx| {
                        _view.update(cx, |view, cx| view.smoke_begin(case.into(), window, cx))
                    })
                    .expect("smoke window")
                    .expect("smoke launch");
            }
            cx.activate(true);
        });
}

#[cfg(feature = "ui-validation")]
async fn verify_effects(handle: AnyWindowHandle, view: &Entity<ui::Launcher>, cx: &mut AsyncApp) {
    let output = bloodborne_launcher::model::find_root().expect("root")
        .join("out/windows-facegen-20261010/ui");
    std::fs::create_dir_all(&output).expect("effects UI output");
    let fixture = tempfile::tempdir().expect("effects fixture");
    for (width, height) in [(1180., 820.), (1000., 680.)] {
        let profile = fixture.path().join(format!("effects-{width}.ini"));
        handle.update(cx, |_, window, cx| {
            window.resize(size(px(width), px(height)));
            view.update(cx, |view, cx| view.smoke_effect_profile(profile, cx));
            window.refresh();
        }).expect("show effects");
        cx.background_executor().timer(std::time::Duration::from_millis(900)).await;
        for expected in [false, true, false] {
            handle.update(cx, |_, window, cx| {
                let checkbox = gpui_kit::base::test_support::find(window, &[], &"vertex_explosion_fix".into())
                    .expect("vertex fix checkbox");
                assert!(checkbox.visible());
                let bounds = checkbox.bounds();
                assert!(f32::from(bounds.origin.y) >= 0.);
                assert!(f32::from(bounds.origin.y + bounds.size.height) <= height);
                assert_eq!(view.read(cx).smoke_vertex_fix(), expected);
                view.update(cx, |view, _| assert_eq!(view.smoke_save_reload_effects(), expected));
                window.render_to_image().expect("effects frame")
                    .save(output.join(format!("effects-{width}-{expected}.png"))).expect("save effects frame");
                smoke_click(window, cx, "vertex_explosion_fix");
            }).expect("check and toggle vertex fix");
            cx.background_executor().timer(std::time::Duration::from_millis(300)).await;
        }
    }
    std::fs::write(output.join("summary.json"),
        "{\"checkbox_on_off\":true,\"save_reload\":true,\"default_off\":true,\"widths\":[1180,1000]}\n")
        .expect("effects UI summary");
}

#[cfg(feature = "ui-validation")]
async fn verify_launch_options(handle: AnyWindowHandle, view: &Entity<ui::Launcher>, cx: &mut AsyncApp) {
    let output = bloodborne_launcher::model::find_root().expect("root")
        .join("out/windows-launch-options-20261010/ui");
    std::fs::create_dir_all(&output).expect("UI output");
    let fixture = tempfile::tempdir().expect("preferences fixture");
    for (width, height) in [(1180., 820.), (1000., 680.)] {
        handle.update(cx, |_, window, cx| {
            window.resize(size(px(width), px(height)));
            view.update(cx, |view, cx| view.smoke_page(0, 0., cx));
            window.refresh();
        }).expect("show Runtime card");
        cx.background_executor().timer(std::time::Duration::from_millis(1000)).await;
        handle.update(cx, |_, window, cx| {
            view.update(cx, |view, cx| view.smoke_scroll_to_bottom(cx));
            window.refresh();
        }).expect("scroll to Runtime");
        cx.background_executor().timer(std::time::Duration::from_millis(500)).await;
        handle.update(cx, |_, window, cx| {
            window.render_to_image().unwrap().save(output.join(format!("before-runtime-{width}.png"))).unwrap();
            let field = gpui_kit::base::test_support::find(window, &[], &"launch-options-field".into())
                .expect("custom launch field");
            assert!(field.visible());
            let bounds = field.bounds();
            assert!(f32::from(bounds.size.width) > 400.);
            assert!(f32::from(bounds.origin.y) >= 0.);
            assert!(f32::from(bounds.origin.y + bounds.size.height) <= height);
            smoke_click(window, cx, "launch-options-field");
            window.dispatch_keystroke(Keystroke::parse("ctrl-a").unwrap(), cx);
            window.dispatch_keystroke(Keystroke::parse("backspace").unwrap(), cx);
            for ch in "-MemoryModel old -FpsLimit 90".chars() {
                window.dispatch_keystroke(Keystroke {
                    key: if ch == ' ' { "space".into() } else { ch.to_string() },
                    key_char: Some(ch.to_string()), ..Default::default()
                }, cx);
            }
        }).expect("type options");
        cx.background_executor().timer(std::time::Duration::from_millis(400)).await;
        handle.update(cx, |_, window, cx| {
            let prefs = view.read(cx).smoke_preferences();
            assert_eq!(prefs.launch_options, "-MemoryModel old -FpsLimit 90");
            prefs.save(fixture.path()).expect("save fixture");
            assert_eq!(bloodborne_launcher::model::Preferences::load(fixture.path()).unwrap().launch_options,
                       prefs.launch_options);
            window.render_to_image().unwrap().save(output.join(format!("runtime-{width}.png"))).unwrap();
            window.dispatch_keystroke(Keystroke::parse("ctrl-a").unwrap(), cx);
            window.dispatch_keystroke(Keystroke::parse("backspace").unwrap(), cx);
        }).expect("check persisted input");
        cx.background_executor().timer(std::time::Duration::from_millis(200)).await;
        handle.update(cx, |_, _, cx| {
            let prefs = view.read(cx).smoke_preferences();
            assert!(prefs.launch_options.is_empty());
            let args = bloodborne_launcher::session::arguments(fixture.path(), prefs, &output.join("log")).unwrap();
            let index = args.iter().position(|arg| arg == "-MemoryModel").unwrap();
            assert_eq!(args[index + 1], "new");
        }).expect("clearing restores default");
    }
    std::fs::write(output.join("summary.json"),
        "{\"typing\":true,\"preference_fixture_saved\":true,\"empty_defaults_new\":true,\"widths\":[1180,1000]}\n")
        .expect("UI summary");
}

#[cfg(feature = "ui-validation")]
fn smoke_click(window: &mut Window, cx: &mut App, id: &str) {
    let target = gpui_kit::base::test_support::find(window, &[], &id.to_owned().into())
        .unwrap_or_else(|| panic!("missing native control: {id}"));
    assert!(
        target.visible() && target.disabled() != Some(true),
        "{id} is not interactive"
    );
    let position = target.bounds().center();
    window.dispatch_event(
        PlatformInput::MouseMove(MouseMoveEvent {
            position,
            ..Default::default()
        }),
        cx,
    );
    window.dispatch_event(
        PlatformInput::MouseDown(MouseDownEvent {
            position,
            button: MouseButton::Left,
            click_count: 1,
            ..Default::default()
        }),
        cx,
    );
    window.dispatch_event(
        PlatformInput::MouseUp(MouseUpEvent {
            position,
            button: MouseButton::Left,
            click_count: 1,
            ..Default::default()
        }),
        cx,
    );
}

#[cfg(feature = "ui-validation")]
async fn verify_motion(handle: AnyWindowHandle, view: &Entity<ui::Launcher>, cx: &mut AsyncApp) {
    let output = bloodborne_launcher::model::find_root()
        .expect("root")
        .join("out/windows-launcher/rust-ui-smoke");
    std::fs::create_dir_all(&output).expect("motion snapshots");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(1100))
        .await;
    handle
        .update(cx, |_, window, cx| smoke_click(window, cx, "navigate-1"))
        .expect("native navigation");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(40))
        .await;
    let early = handle
        .update(cx, |_, window, cx| {
            assert_eq!(view.read(cx).smoke_current_page(), 1);
            let frame = window.render_to_image().expect("early transition frame");
            frame
                .save(output.join("motion-enter.png"))
                .expect("save early frame");
            frame.into_raw()
        })
        .expect("early animation snapshot");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(450))
        .await;
    let settled = handle
        .update(cx, |_, window, _| {
            let frame = window.render_to_image().expect("settled transition frame");
            frame
                .save(output.join("motion-settled.png"))
                .expect("save settled frame");
            frame.into_raw()
        })
        .expect("settled animation snapshot");
    assert_ne!(
        early, settled,
        "navigation transition must produce intermediate frames"
    );

    // Interrupt an in-flight spring with several genuine clicks, rather than
    // calling the navigation method directly. The final selection must win.
    for page in [2, 3, 4, 0, 1, 3, 2, 0, 4, 1] {
        handle
            .update(cx, |_, window, cx| {
                smoke_click(window, cx, &format!("navigate-{page}"));
                assert_eq!(view.read(cx).smoke_current_page(), page);
            })
            .expect("rapid navigation click");
        cx.background_executor()
            .timer(std::time::Duration::from_millis(55))
            .await;
    }
    handle
        .update(cx, |_, window, cx| {
            view.update(cx, |view, cx| view.smoke_page(1, 0., cx));
            window.dispatch_event(
                PlatformInput::MouseMove(MouseMoveEvent {
                    position: point(px(400.), px(110.)),
                    ..Default::default()
                }),
                cx,
            );
            window.refresh();
        })
        .expect("restore display section");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(500))
        .await;
    handle
        .update(cx, |_, window, cx| {
            smoke_click(window, cx, "select-output_res")
        })
        .expect("open native select");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(250))
        .await;
    handle
        .update(cx, |_, window, cx| {
            let select =
                gpui_kit::base::test_support::find(window, &[], &"select-output_res".into())
                    .expect("select root");
            assert_eq!(
                select.expanded(),
                Some(true),
                "select must open during animated UI"
            );
            window
                .render_to_image()
                .expect("popover frame")
                .save(output.join("motion-select.png"))
                .expect("save select");
            window.dispatch_event(
                PlatformInput::KeyDown(KeyDownEvent {
                    keystroke: Keystroke::parse("escape").expect("escape"),
                    is_held: false,
                    prefer_character_input: false,
                }),
                cx,
            );
        })
        .expect("native select state");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(250))
        .await;
    // Select each real window-mode option using native keyboard navigation.
    let original_mode = handle
        .update(cx, |_, window, cx| {
            assert_eq!(
                gpui_kit::base::test_support::find(window, &[], &"select-output_res".into())
                    .expect("select root")
                    .expanded(),
                Some(false)
            );
            view.read(cx).smoke_window_mode().to_owned()
        })
        .expect("current mode");
    for target in [
        "borderless",
        "fullscreen",
        "windowed",
        original_mode.as_str(),
    ] {
        let current = handle
            .update(cx, |_, window, cx| {
                let mode = view.read(cx).smoke_window_mode().to_owned();
                smoke_click(window, cx, "select-window_mode");
                mode
            })
            .expect("open window modes");
        cx.background_executor()
            .timer(std::time::Duration::from_millis(200))
            .await;
        handle
            .update(cx, |_, window, cx| {
                let modes = bloodborne_launcher::model::WINDOW_MODES;
                let from = modes
                    .iter()
                    .position(|&m| m == current)
                    .expect("current mode index") as i32;
                let to = modes
                    .iter()
                    .position(|&m| m == target)
                    .expect("target mode index") as i32;
                for key in std::iter::repeat_n(
                    if to > from { "down" } else { "up" },
                    (to - from).unsigned_abs() as usize,
                )
                .chain(std::iter::once("enter"))
                {
                    let keystroke = Keystroke::parse(key).expect("select navigation key");
                    window.dispatch_event(
                        PlatformInput::KeyDown(KeyDownEvent {
                            keystroke: keystroke.clone(),
                            is_held: false,
                            prefer_character_input: false,
                        }),
                        cx,
                    );
                    window.dispatch_event(PlatformInput::KeyUp(KeyUpEvent { keystroke }), cx);
                }
            })
            .expect("select window mode");
        cx.background_executor()
            .timer(std::time::Duration::from_millis(250))
            .await;
        handle
            .update(cx, |_, window, cx| {
                assert_eq!(view.read(cx).smoke_window_mode(), target);
                window
                    .render_to_image()
                    .expect("window mode frame")
                    .save(output.join(format!("window-mode-{target}.png")))
                    .expect("save window mode");
            })
            .expect("verify native window-mode selection");
    }
    handle
        .update(cx, |_, window, cx| {
            view.update(cx, |view, cx| view.smoke_page(0, 0., cx));
            window.refresh();
        })
        .expect("show switch page");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(400))
        .await;
    let use_patch = handle
        .update(cx, |_, window, cx| {
            let initial = view.read(cx).smoke_use_patch();
            smoke_click(window, cx, "@use_patch");
            initial
        })
        .expect("switch click");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(350))
        .await;
    handle
        .update(cx, |_, window, cx| {
            assert_eq!(view.read(cx).smoke_use_patch(), !use_patch);
            window
                .render_to_image()
                .expect("switch frame")
                .save(output.join("motion-switch.png"))
                .expect("save switch");
            smoke_click(window, cx, "@use_patch");
        })
        .expect("switch round trip");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(500))
        .await;
    let idle = handle.update(cx, |_, window, cx| {
        assert_eq!(view.read(cx).smoke_use_patch(), use_patch);
        view.update(cx, |view, cx| view.smoke_page(1, 0., cx));
        window.refresh();
        window.dispatch_event(
            PlatformInput::MouseMove(MouseMoveEvent {
                position: point(px(400.), px(110.)),
                ..Default::default()
            }),
            cx,
        );
    });
    idle.expect("restore pointer");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(400))
        .await;
    let first = handle
        .update(cx, |_, window, _| {
            window.render_to_image().expect("idle frame").into_raw()
        })
        .expect("first idle snapshot");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(400))
        .await;
    let second = handle
        .update(cx, |_, window, _| {
            window.render_to_image().expect("idle frame").into_raw()
        })
        .expect("second idle snapshot");
    assert_eq!(first, second, "settled UI must stop changing");
    handle
        .update(cx, |_, window, cx| {
            cx.set_reduce_motion(true);
            smoke_click(window, cx, "navigate-0");
        })
        .expect("reduced motion navigation");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(150))
        .await;
    handle
        .update(cx, |_, window, cx| {
            assert_eq!(view.read(cx).smoke_current_page(), 0);
            window
                .render_to_image()
                .expect("reduced motion frame")
                .save(output.join("motion-reduced.png"))
                .expect("save reduced motion");
            cx.set_reduce_motion(false);
        })
        .expect("reduced motion snapshot");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(400))
        .await;
    let normal_color = handle
        .update(cx, |_, window, cx| {
            let bounds = gpui_kit::base::test_support::find(window, &[], &"hero-launch".into())
                .expect("primary button")
                .bounds();
            let sample = point(bounds.center().x, bounds.origin.y + px(5.));
            let color = window
                .render_to_image()
                .expect("normal primary frame")
                .get_pixel(f32::from(sample.x) as u32, f32::from(sample.y) as u32)
                .0;
            window.dispatch_event(
                PlatformInput::MouseMove(MouseMoveEvent {
                    position: bounds.center(),
                    ..Default::default()
                }),
                cx,
            );
            (sample, color)
        })
        .expect("hover primary button");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(40))
        .await;
    let middle_color = handle
        .update(cx, |_, window, _| {
            let frame = window
                .render_to_image()
                .expect("primary intermediate frame");
            frame
                .save(output.join("motion-button-middle.png"))
                .expect("save primary transition");
            frame
                .get_pixel(
                    f32::from(normal_color.0.x) as u32,
                    f32::from(normal_color.0.y) as u32,
                )
                .0
        })
        .expect("primary hover intermediate");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(250))
        .await;
    let final_color = handle
        .update(cx, |_, window, _| {
            let frame = window.render_to_image().expect("primary hover frame");
            frame
                .save(output.join("motion-button-hover.png"))
                .expect("save primary hover");
            frame
                .get_pixel(
                    f32::from(normal_color.0.x) as u32,
                    f32::from(normal_color.0.y) as u32,
                )
                .0
        })
        .expect("primary hover final");
    assert_ne!(
        normal_color.1, middle_color,
        "button must start changing on hover"
    );
    assert_ne!(
        middle_color, final_color,
        "button must interpolate instead of jumping to hover"
    );
    assert!(
        final_color[0] > normal_color.1[0],
        "primary hover should brighten"
    );
    // Exercise press feedback on navigation, which has no disk/game side effect.
    handle
        .update(cx, |_, window, cx| {
            let position = gpui_kit::base::test_support::find(window, &[], &"navigate-0".into())
                .expect("navigation button")
                .bounds()
                .center();
            window.dispatch_event(
                PlatformInput::MouseMove(MouseMoveEvent {
                    position,
                    ..Default::default()
                }),
                cx,
            );
            window.dispatch_event(
                PlatformInput::MouseDown(MouseDownEvent {
                    position,
                    button: MouseButton::Left,
                    click_count: 1,
                    ..Default::default()
                }),
                cx,
            );
        })
        .expect("hold navigation button");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(220))
        .await;
    handle
        .update(cx, |_, window, cx| {
            window
                .render_to_image()
                .expect("pressed frame")
                .save(output.join("motion-button-pressed.png"))
                .expect("save press");
            let position = point(px(400.), px(55.));
            window.dispatch_event(
                PlatformInput::MouseMove(MouseMoveEvent {
                    position,
                    ..Default::default()
                }),
                cx,
            );
            window.dispatch_event(
                PlatformInput::MouseUp(MouseUpEvent {
                    position,
                    button: MouseButton::Left,
                    click_count: 1,
                    ..Default::default()
                }),
                cx,
            );
            assert_eq!(view.read(cx).smoke_current_page(), 0);
        })
        .expect("release outside button");
    std::fs::write(output.join("motion-checks.json"), serde_json::json!({"success":true,"native_navigation_clicks":12,"intermediate_frames":true,"select_open_close":true,"switch_round_trip":true,"idle_frames_equal":true,"reduced_motion":true,"primary_hover_interpolates":true,"primary_colors":[normal_color.1,middle_color,final_color],"press_cancel_outside":true}).to_string()).expect("motion report");
}

#[cfg(feature = "ui-validation")]
async fn verify_mods(handle: AnyWindowHandle, view: &Entity<ui::Launcher>, cx: &mut AsyncApp) {
    use bloodborne_launcher::{model, mods};
    let output = model::find_root()
        .expect("root")
        .join("out/windows-mod-review/native");
    std::fs::create_dir_all(&output).expect("mod smoke folder");
    let mut entries = Vec::new();
    for (name, layout) in [("Hunter outfit", "dvdroot_ps4/chr"), ("Lighting", "map")] {
        let folder = output.join(name);
        std::fs::create_dir_all(folder.join(layout)).expect("fixture mod folder");
        std::fs::write(folder.join(layout).join("sample.dcx"), name).expect("fixture mod file");
        entries.push(mods::inspect(&folder).expect("detect fixture mod"));
    }
    handle
        .update(cx, |_, window, cx| {
            view.update(cx, |view, cx| {
                view.smoke_mods(entries, output.join("settings.json"), cx)
            });
            window.refresh();
        })
        .expect("mod page");
    cx.background_executor()
        .timer(std::time::Duration::from_millis(900))
        .await;
    for (id, snapshot) in [
        ("mod-toggle-0", "mod-off"),
        ("mods-enabled", "all-off"),
        ("mods-enabled", "global-on"),
        ("mod-down-0", "reordered"),
        ("mod-remove-0", "removed"),
    ] {
        handle
            .update(cx, |_, window, cx| smoke_click(window, cx, id))
            .expect("native mod control");
        cx.background_executor()
            .timer(std::time::Duration::from_millis(450))
            .await;
        handle
            .update(cx, |_, window, cx| {
                let (entries, enabled) = view.read(cx).smoke_mod_entries();
                match snapshot {
                    "mod-off" => {
                        assert!(enabled);
                        assert!(!entries[0].enabled);
                        assert!(entries[1].enabled);
                    }
                    "all-off" => {
                        assert!(!enabled);
                        assert!(!entries[0].enabled);
                        assert!(entries[1].enabled);
                    }
                    "global-on" => {
                        assert!(enabled);
                        assert!(!entries[0].enabled);
                        assert!(entries[1].enabled);
                    }
                    "reordered" => {
                        assert_eq!(entries[0].name, "Lighting");
                        assert_eq!(entries[1].name, "Hunter outfit");
                    }
                    "removed" => {
                        assert_eq!(entries.len(), 1);
                        assert_eq!(entries[0].name, "Hunter outfit");
                    }
                    _ => unreachable!(),
                }
                let disk: serde_json::Value = serde_json::from_str(
                    &std::fs::read_to_string(output.join("settings.json")).expect("saved prefs"),
                )
                .expect("valid prefs");
                assert_eq!(disk["mods_enabled"].as_bool(), Some(enabled));
                assert_eq!(disk["mods"].as_array().unwrap().len(), entries.len());
                for (index, entry) in entries.iter().enumerate() {
                    assert_eq!(
                        disk["mods"][index]["name"].as_str(),
                        Some(entry.name.as_str())
                    );
                    assert_eq!(
                        disk["mods"][index]["enabled"].as_bool(),
                        Some(entry.enabled)
                    );
                }
                window
                    .render_to_image()
                    .expect("mod snapshot")
                    .save(output.join(format!("{snapshot}.png")))
                    .expect("save mod snapshot");
            })
            .expect("mod assertions");
    }
    for name in ["Hunter outfit/dvdroot_ps4/chr", "Lighting/map"] {
        assert!(
            output.join(name).join("sample.dcx").is_file(),
            "Remove modified source files"
        );
    }
    for (width, height) in [(1000., 680.), (1440., 920.)] {
        handle
            .update(cx, |_, window, _| {
                window.resize(size(px(width), px(height)))
            })
            .expect("resize mod page");
        cx.background_executor()
            .timer(std::time::Duration::from_millis(600))
            .await;
        handle
            .update(cx, |_, window, _| {
                window
                    .render_to_image()
                    .expect("mod resized frame")
                    .save(output.join(format!("mods-{width:.0}.png")))
                    .expect("resized snapshot");
            })
            .expect("resize snapshot");
    }
    std::fs::write(output.join("summary.json"), "{\"native_switches\":3,\"reorder\":true,\"remove\":true,\"settings_saved\":true,\"source_files_unchanged\":true}\n").expect("smoke report");
    cx.update(|cx| cx.quit());
}

#[cfg(feature = "ui-validation")]
async fn verify_controller_clicks(
    handle: AnyWindowHandle,
    view: &Entity<ui::Launcher>,
    cx: &mut AsyncApp,
    output: &std::path::Path,
    width: u32,
) -> usize {
    // Exercise actual rendered hit targets, including clicking Cross again after another action.
    let targets = [
        ("diagram-circle", "circle"),
        ("diagram-cross", "cross"),
        ("diagram-triangle", "triangle"),
        ("diagram-square", "square"),
        ("diagram-l1", "l1"),
        ("diagram-l2", "l2"),
        ("diagram-r1", "r1"),
        ("diagram-r2", "r2"),
        ("diagram-label-l1", "l1"),
        ("diagram-label-l2", "l2"),
        ("diagram-label-r1", "r1"),
        ("diagram-label-r2", "r2"),
        ("diagram-l3", "l3"),
        ("diagram-r3", "r3"),
        ("diagram-touchpad_left", "touchpad_left"),
        ("diagram-touchpad_right", "touchpad_right"),
        ("diagram-options", "options"),
        ("diagram-dpad_up", "dpad_up"),
        ("diagram-dpad_down", "dpad_down"),
        ("diagram-dpad_left", "dpad_left"),
        ("diagram-dpad_right", "dpad_right"),
        ("diagram-cross", "cross"),
    ];
    let mut geometry = Vec::new();
    for (index, (id, expected)) in targets.iter().enumerate() {
        for (sample, (dx, dy)) in [(0., 0.), (-0.4, 0.), (0.4, 0.), (0., -0.4), (0., 0.4)]
            .into_iter()
            .enumerate()
        {
            let bounds = handle
                .update(cx, |_, window, cx| {
                    let target = gpui_kit::base::test_support::find(window, &[], &(*id).into())
                        .expect("rendered controller target");
                    assert!(target.visible(), "{id} must be visible");
                    let bounds = target.bounds();
                    let source = if id.starts_with("diagram-label-") {
                        controller::SHOULDER_LABELS
                            .iter()
                            .find(|(key, _)| key == expected)
                            .expect("label geometry")
                            .1
                    } else {
                        controller::CONTROLS
                            .iter()
                            .find(|control| control.key == *expected)
                            .expect("control geometry")
                            .bounds
                    };
                    let cross =
                        gpui_kit::base::test_support::find(window, &[], &"diagram-cross".into())
                            .expect("cross anchor")
                            .bounds();
                    // Use widely separated anchors: rounding a 40px button width
                    // would magnify a subpixel error across the whole diagram.
                    let left =
                        gpui_kit::base::test_support::find(window, &[], &"diagram-label-l2".into())
                            .expect("left anchor")
                            .bounds();
                    let right =
                        gpui_kit::base::test_support::find(window, &[], &"diagram-label-r2".into())
                            .expect("right anchor")
                            .bounds();
                    let scale = f32::from(right.center().x - left.center().x) / (1580. - 92.);
                    let origin_x = f32::from(left.center().x) - 92. * scale;
                    let origin_y = f32::from(cross.center().y) - 491. * scale;
                    for (actual, expected_value) in [
                        (f32::from(bounds.center().x), origin_x + source[0] * scale),
                        (f32::from(bounds.center().y), origin_y + source[1] * scale),
                        (f32::from(bounds.size.width), source[2] * scale),
                        (f32::from(bounds.size.height), source[3] * scale),
                    ] {
                        assert!(
                            (actual - expected_value).abs() < 1.1,
                            "{id} is misaligned: {actual} vs {expected_value}"
                        );
                    }
                    let position =
                        bounds.center() + point(bounds.size.width * dx, bounds.size.height * dy);
                    window.dispatch_event(
                        PlatformInput::MouseMove(MouseMoveEvent {
                            position,
                            ..Default::default()
                        }),
                        cx,
                    );
                    window.dispatch_event(
                        PlatformInput::MouseDown(MouseDownEvent {
                            position,
                            button: MouseButton::Left,
                            click_count: 1,
                            ..Default::default()
                        }),
                        cx,
                    );
                    window.dispatch_event(
                        PlatformInput::MouseUp(MouseUpEvent {
                            position,
                            button: MouseButton::Left,
                            click_count: 1,
                            ..Default::default()
                        }),
                        cx,
                    );
                    bounds
                })
                .expect("click rendered target");
            cx.background_executor()
                .timer(std::time::Duration::from_millis(80))
                .await;
            handle
                .update(cx, |_, window, cx| {
                    assert_eq!(
                        view.read(cx).smoke_selection(),
                        *expected,
                        "{id} selected the wrong binding at sample {sample}"
                    );
                    if sample == 0 {
                        window
                            .render_to_image()
                            .expect("selection frame")
                            .save(
                                output.join(format!("selection-{width}-{index:02}-{expected}.png")),
                            )
                            .expect("selection snapshot");
                    }
                })
                .expect("verify clicked binding");
            if sample == 0 {
                geometry.push(serde_json::json!({"id": id, "center": [f32::from(bounds.center().x), f32::from(bounds.center().y)], "size": [f32::from(bounds.size.width), f32::from(bounds.size.height)]}));
            }
        }
    }
    std::fs::write(
        output.join(format!("controller-geometry-{width}.json")),
        serde_json::to_string_pretty(&geometry).expect("geometry JSON"),
    )
    .expect("geometry report");
    targets.len() * 5
}
