// SPDX-License-Identifier: GPL-2.0-or-later
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeSet,
    fs,
    path::{Path, PathBuf},
};

const FOLDERS: &[&str] = &[
    "action", "chr", "event", "facegen", "font", "map", "menu", "movie", "msg", "mtd", "obj",
    "other", "param", "paramdef", "parts", "remo", "script", "sfx", "shader", "sound",
];

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ModEntry {
    pub name: String,
    pub path: String,
    pub enabled: bool,
    pub files: usize,
    pub folders: Vec<String>,
    pub layout: String,
}

fn check(path: &Path) -> Result<fs::Metadata> {
    let meta = fs::symlink_metadata(path).with_context(|| format!("Read {}", path.display()))?;
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        ensure!(
            meta.file_attributes() & 0x400 == 0,
            "Mod links are unsupported: {}",
            path.display()
        );
    }
    ensure!(
        !meta.is_symlink(),
        "Mod links are unsupported: {}",
        path.display()
    );
    Ok(meta)
}

fn detect(folder: &Path, depth: usize) -> Result<(PathBuf, String, bool)> {
    ensure!(
        depth <= 5 && check(folder)?.is_dir(),
        "Select an extracted mod folder."
    );
    let mut dirs = Vec::new();
    for entry in fs::read_dir(folder)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            dirs.push(entry.path());
        }
    }
    dirs.sort();
    let name = |p: &Path| {
        p.file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .to_ascii_lowercase()
    };
    if let Some(dvd) = dirs.iter().find(|p| name(p) == "dvdroot_ps4") {
        check(dvd)?;
        return Ok((dvd.clone(), "dvdroot_ps4".into(), false));
    }
    if name(folder) == "dvdroot_ps4" {
        return Ok((folder.into(), "dvdroot_ps4".into(), false));
    }
    if dirs.iter().any(|p| FOLDERS.contains(&name(p).as_str())) {
        return Ok((folder.into(), "Asset folders".into(), true));
    }
    if FOLDERS.contains(&name(folder).as_str()) {
        return Ok((
            folder.into(),
            format!("dvdroot_ps4/{}", name(folder)),
            false,
        ));
    }
    let dirs: Vec<_> = dirs
        .into_iter()
        .filter(|p| !name(p).starts_with('.'))
        .collect();
    if dirs.len() == 1 {
        return detect(&dirs[0], depth + 1);
    }
    anyhow::bail!("No dvdroot_ps4 or game asset folders found. Select the extracted mod folder.")
}

fn count(
    folder: &Path,
    root: &Path,
    bare: bool,
    seen: &mut BTreeSet<String>,
    files: &mut usize,
    groups: &mut BTreeSet<String>,
) -> Result<()> {
    for entry in fs::read_dir(folder)? {
        let path = entry?.path();
        let relative = path.strip_prefix(root)?;
        let group = relative
            .components()
            .next()
            .unwrap()
            .as_os_str()
            .to_string_lossy()
            .to_ascii_lowercase();
        if bare && !FOLDERS.contains(&group.as_str()) {
            continue;
        }
        let meta = check(&path)?;
        ensure!(
            seen.insert(relative.to_string_lossy().to_ascii_lowercase()),
            "Case-conflicting mod files: {}",
            path.display()
        );
        if meta.is_dir() {
            count(&path, root, bare, seen, files, groups)?;
        } else {
            ensure!(meta.is_file(), "Not a regular mod file: {}", path.display());
            *files += 1;
            if FOLDERS.contains(&group.as_str()) {
                groups.insert(group);
            }
        }
    }
    Ok(())
}

pub fn inspect(folder: &Path) -> Result<ModEntry> {
    check(folder)?;
    let folder = crate::model::resolve(Path::new("."), &folder.to_string_lossy())?;
    let (content, layout, bare) = detect(&folder, 0)?;
    let mut files = 0;
    let mut folders = BTreeSet::new();
    count(
        &content,
        &content,
        bare,
        &mut BTreeSet::new(),
        &mut files,
        &mut folders,
    )?;
    ensure!(
        files > 0,
        "No loose game files found in {}",
        folder.display()
    );
    if layout.starts_with("dvdroot_ps4/") {
        folders.insert(
            content
                .file_name()
                .unwrap()
                .to_string_lossy()
                .to_ascii_lowercase(),
        );
    }
    Ok(ModEntry {
        name: folder
            .file_name()
            .context("Missing mod folder name")?
            .to_string_lossy()
            .into(),
        path: folder.to_string_lossy().into(),
        enabled: true,
        files,
        folders: folders.into_iter().collect(),
        layout,
    })
}

pub fn add(entries: &mut Vec<ModEntry>, entry: ModEntry) -> Result<()> {
    ensure!(
        !entries
            .iter()
            .any(|m| Path::new(&m.path) == Path::new(&entry.path)),
        "This mod folder is already in your list."
    );
    entries.push(entry);
    Ok(())
}

pub fn move_entry(entries: &mut [ModEntry], index: usize, direction: isize) {
    if let Some(next) = index.checked_add_signed(direction)
        && index < entries.len()
        && next < entries.len()
    {
        entries.swap(index, next);
    }
}
