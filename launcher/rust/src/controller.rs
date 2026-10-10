// SPDX-License-Identifier: GPL-2.0-or-later
//! Source-space hit regions and selection contours for the supplied 1672x941 diagram.
//! Selection artwork and hit areas are separate: large click targets never distort a contour.

pub const WIDTH: f32 = 1672.;
pub const HEIGHT: f32 = 941.;

pub struct Control {
    pub key: &'static str,
    pub label: &'static str,
    /// Center x/y and width/height, in original image pixels.
    pub bounds: [f32; 4],
    pub contour: &'static str,
}

pub const CONTROLS: &[Control] = &[
    Control {
        key: "l2",
        label: "L2",
        bounds: [498., 185., 72., 32.],
        contour: r#"<path d="M421 203 Q480 178 551 198 L551 208 Q482 187 421 215 Z"/><path d="M136 77 H393 L498 185" fill="none"/><rect x="64" y="55" width="56" height="44" rx="9"/>"#,
    },
    Control {
        key: "l1",
        label: "L1",
        bounds: [409., 211., 72., 32.],
        contour: r#"<path d="M401 226 Q479 192 556 219 L579 229 Q482 201 396 235 Z"/><path d="M136 180 H378 L409 211" fill="none"/><rect x="64" y="158" width="56" height="44" rx="9"/>"#,
    },
    Control {
        key: "r2",
        label: "R2",
        bounds: [1174., 185., 72., 32.],
        contour: r#"<g transform="translate(1672 0) scale(-1 1)"><path d="M421 203 Q480 178 551 198 L551 208 Q482 187 421 215 Z"/><path d="M136 77 H393 L498 185" fill="none"/></g><rect x="1545" y="55" width="60" height="44" rx="9"/>"#,
    },
    Control {
        key: "r1",
        label: "R1",
        bounds: [1264., 211., 72., 32.],
        contour: r#"<g transform="translate(1672 0) scale(-1 1)"><path d="M401 226 Q479 192 556 219 L579 229 Q482 201 396 235 Z"/><path d="M136 180 H378 L409 211" fill="none"/></g><rect x="1541" y="158" width="60" height="44" rx="9"/>"#,
    },
    Control {
        key: "dpad_up",
        label: "D-pad Up",
        bounds: [467., 348., 70., 82.],
        contour: r#"<path d="M467 386 C461 386 438 365 437 357 L435 326 C431 305 503 305 499 326 L497 357 C496 365 473 386 467 386 Z"/>"#,
    },
    Control {
        key: "dpad_down",
        label: "D-pad Down",
        bounds: [467., 460., 70., 82.],
        contour: r#"<path transform="translate(0 808) scale(1 -1)" d="M467 386 C461 386 438 365 437 357 L435 326 C431 305 503 305 499 326 L497 357 C496 365 473 386 467 386 Z"/>"#,
    },
    Control {
        key: "dpad_left",
        label: "D-pad Left",
        bounds: [410., 404., 78., 68.],
        contour: r#"<path d="M449 403 C449 410 431 432 421 434 L387 437 Q370 437 372 408 L373 389 Q372 371 387 373 L422 373 Q432 374 449 398 Z"/>"#,
    },
    Control {
        key: "dpad_right",
        label: "D-pad Right",
        bounds: [524., 404., 78., 68.],
        contour: r#"<path transform="translate(934 0) scale(-1 1)" d="M449 403 C449 410 431 432 421 434 L387 437 Q370 437 372 408 L373 389 Q372 371 387 373 L422 373 Q432 374 449 398 Z"/>"#,
    },
    Control {
        key: "triangle",
        label: "Triangle",
        bounds: [1201., 321., 80., 80.],
        contour: r#"<circle cx="1201" cy="321" r="38"/>"#,
    },
    Control {
        key: "cross",
        label: "Cross",
        bounds: [1201., 491., 76., 76.],
        contour: r#"<circle cx="1201" cy="491" r="35"/>"#,
    },
    Control {
        key: "square",
        label: "Square",
        bounds: [1115., 405., 80., 80.],
        contour: r#"<circle cx="1115" cy="405" r="37"/>"#,
    },
    Control {
        key: "circle",
        label: "Circle",
        bounds: [1288., 405., 80., 80.],
        contour: r#"<circle cx="1288" cy="405" r="37"/>"#,
    },
    Control {
        key: "l3",
        label: "L3",
        bounds: [644., 561., 142., 142.],
        contour: r#"<circle cx="644" cy="561" r="69"/>"#,
    },
    Control {
        key: "r3",
        label: "R3",
        bounds: [1027., 561., 142., 142.],
        contour: r#"<circle cx="1027" cy="561" r="69"/>"#,
    },
    Control {
        key: "touchpad_left",
        label: "Touchpad left",
        bounds: [744., 334., 182., 214.],
        contour: r#"<path d="M673 227 H835 V440 H673 Q654 440 654 421 V247 Q654 227 673 227 Z"/>"#,
    },
    Control {
        key: "touchpad_right",
        label: "Touchpad right",
        bounds: [927., 334., 182., 214.],
        contour: r#"<path d="M835 227 H999 Q1018 227 1018 247 V421 Q1018 440 999 440 H835 Z"/>"#,
    },
    Control {
        key: "options",
        label: "Options",
        bounds: [1067., 292., 44., 74.],
        contour: r#"<rect x="1049" y="258" width="37" height="68" rx="18"/>"#,
    },
];

pub const SHOULDER_LABELS: &[(&str, [f32; 4])] = &[
    ("l2", [92., 77., 110., 62.]),
    ("l1", [92., 180., 110., 62.]),
    ("r2", [1580., 77., 110., 62.]),
    ("r1", [1580., 180., 110., 62.]),
];

pub fn selection_svg(key: &str) -> Option<Vec<u8>> {
    let control = CONTROLS.iter().find(|control| control.key == key)?;
    Some(format!(
        r##"<svg xmlns="http://www.w3.org/2000/svg" width="836" height="470.5" viewBox="0 0 1672 941"><g fill="#cbb082" fill-opacity="0.14" stroke="#e4c590" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">{}</g></svg>"##,
        control.contour
    ).into_bytes())
}
