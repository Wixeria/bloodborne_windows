// SPDX-License-Identifier: GPL-2.0-or-later
use gpui_kit::{AssetSource, SharedString};
use std::borrow::Cow;

/// App artwork is embedded alongside the component icons: no network or loose files.
pub struct LauncherAssets;
impl AssetSource for LauncherAssets {
    fn load(&self, path: &str) -> anyhow::Result<Option<Cow<'static, [u8]>>> {
        if let Some(key) = path
            .strip_prefix("launcher/selection-")
            .and_then(|s| s.strip_suffix(".svg"))
        {
            return Ok(crate::controller::selection_svg(key).map(Cow::Owned));
        }
        match path {
            "launcher/puzzle.svg" => {
                Ok(Some(Cow::Borrowed(include_bytes!("../assets/puzzle.svg"))))
            }
            "launcher/brand-cover.jpg" => Ok(Some(Cow::Borrowed(include_bytes!(
                "../assets/brand-cover.jpg"
            )))),
            "launcher/hunter-workshop.png" => Ok(Some(Cow::Borrowed(include_bytes!(
                "../assets/hunter-workshop.png"
            )))),
            "launcher/controller.png" => Ok(Some(Cow::Borrowed(include_bytes!(
                "../assets/controller.png"
            )))),
            "launcher/controller-dark.svg" => Ok(Some(Cow::Borrowed(include_bytes!(
                "../assets/controller-dark.svg"
            )))),
            _ => gpui_kit::assets::AllAssets.load(path),
        }
    }
    fn list(&self, path: &str) -> anyhow::Result<Vec<SharedString>> {
        let mut result = gpui_kit::assets::AllAssets.list(path)?;
        for name in [
            "launcher/puzzle.svg",
            "launcher/brand-cover.jpg",
            "launcher/hunter-workshop.png",
            "launcher/controller.png",
            "launcher/controller-dark.svg",
        ] {
            if name.starts_with(path) {
                result.push(name.into());
            }
        }
        for control in crate::controller::CONTROLS {
            let name = format!("launcher/selection-{}.svg", control.key);
            if name.starts_with(path) {
                result.push(name.into());
            }
        }
        Ok(result)
    }
}
