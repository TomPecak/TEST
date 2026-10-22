//! Informacje o systemie: miejsca (foldery XDG), zamontowane dyski, ostatnio używane pliki.

use std::collections::HashMap;
use std::ffi::CString;
use std::fs;
use std::path::{Path, PathBuf};

use crate::entry::{self, EntryInfo, SpecialFolders};

pub struct Place {
    pub name: String,
    pub path: String,
    pub icon_name: String,
}

pub fn places() -> Vec<Place> {
    let candidates = [
        (dirs::home_dir(), "user-home"),
        (dirs::desktop_dir(), "user-desktop"),
        (dirs::download_dir(), "folder-download"),
        (dirs::document_dir(), "folder-documents"),
        (dirs::picture_dir(), "folder-pictures"),
        (dirs::audio_dir(), "folder-music"),
        (dirs::video_dir(), "folder-videos"),
    ];
    let home = dirs::home_dir();
    let mut result = Vec::new();
    for (path, icon) in candidates {
        let Some(path) = path else { continue };
        // Foldery XDG ustawione na katalog domowy (brak konfiguracji) pomijamy
        if Some(&path) != home.as_ref() || icon == "user-home" {
            if path.is_dir() && !result.iter().any(|p: &Place| p.path == path.to_string_lossy()) {
                result.push(Place {
                    name: path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_else(|| "/".into()),
                    path: path.to_string_lossy().into_owned(),
                    icon_name: icon.to_string(),
                });
            }
        }
    }
    result
}

pub struct Drive {
    pub name: String,
    pub mount_point: String,
    pub device: String,
    pub fs_type: String,
    pub total: u64,
    pub free: u64,
    pub removable: bool,
}

const REAL_FILESYSTEMS: &[&str] = &[
    "ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "vfat", "exfat", "ntfs", "ntfs3", "fuseblk", "zfs", "bcachefs",
];

/// Zamontowane systemy plików z dysków (bez pseudo-systemów, snapów i partycji rozruchowych).
pub fn drives() -> Vec<Drive> {
    let labels = disk_labels();
    let mounts = fs::read_to_string("/proc/self/mounts").unwrap_or_default();
    let mut seen_devices = Vec::new();
    let mut result = Vec::new();
    for line in mounts.lines() {
        let fields: Vec<&str> = line.split_whitespace().collect();
        if fields.len() < 3 {
            continue;
        }
        let device = unescape_mount(fields[0]);
        let mount_point = unescape_mount(fields[1]);
        let fs_type = fields[2].to_string();
        // Katalog główny pokazujemy zawsze (także overlay w systemie live, btrfs w podwolumenie itp.)
        let is_root = mount_point == "/";
        if (!is_root && !REAL_FILESYSTEMS.contains(&fs_type.as_str()))
            || mount_point.starts_with("/snap")
            || mount_point.starts_with("/boot")
            || mount_point.starts_with("/var/snap")
            || seen_devices.contains(&device)
        {
            continue;
        }
        seen_devices.push(device.clone());
        let (total, free) = disk_space(&mount_point);
        let removable = mount_point.starts_with("/media") || mount_point.starts_with("/run/media");
        let canonical = fs::canonicalize(&device).unwrap_or_else(|_| PathBuf::from(&device));
        let name = labels
            .get(&canonical)
            .cloned()
            .unwrap_or_else(|| if mount_point == "/" { "System".into() } else {
                Path::new(&mount_point).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or(mount_point.clone())
            });
        result.push(Drive { name, mount_point, device, fs_type, total, free, removable });
    }
    result
}

fn disk_labels() -> HashMap<PathBuf, String> {
    let mut labels = HashMap::new();
    if let Ok(entries) = fs::read_dir("/dev/disk/by-label") {
        for entry in entries.flatten() {
            if let Ok(target) = fs::canonicalize(entry.path()) {
                labels.insert(target, unescape_mount(&entry.file_name().to_string_lossy()));
            }
        }
    }
    labels
}

/// /proc/mounts i /dev/disk/by-label kodują spacje itp. jako \040 (ósemkowo)
fn unescape_mount(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'\\' && i + 4 <= bytes.len() && text.is_char_boundary(i + 4) {
            let code = &text[i + 1..i + 4];
            if let Ok(value) = u8::from_str_radix(code, 8) {
                out.push(value);
                i += 4;
                continue;
            }
            if code.starts_with('x') {
                if let Ok(value) = u8::from_str_radix(&text[i + 2..i + 4], 16) {
                    out.push(value);
                    i += 4;
                    continue;
                }
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn disk_space(path: &str) -> (u64, u64) {
    let Ok(c_path) = CString::new(path) else { return (0, 0) };
    let mut stat: libc::statvfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::statvfs(c_path.as_ptr(), &mut stat) } != 0 {
        return (0, 0);
    }
    let block = stat.f_frsize as u64;
    (stat.f_blocks as u64 * block, stat.f_bavail as u64 * block)
}

/// Ostatnio używane pliki z ~/.local/share/recently-used.xbel (standard freedesktop),
/// tylko istniejące, od najnowszych.
pub fn recent(limit: usize, special: &SpecialFolders) -> Vec<EntryInfo> {
    let Some(data) = dirs::data_dir() else { return Vec::new() };
    let Ok(xml) = fs::read_to_string(data.join("recently-used.xbel")) else { return Vec::new() };

    let mut items: Vec<(i64, PathBuf)> = Vec::new();
    for chunk in xml.split("<bookmark ").skip(1) {
        let tag = chunk.split('>').next().unwrap_or("");
        let (Some(href), Some(modified)) = (attribute(tag, "href"), attribute(tag, "modified")) else { continue };
        let Some(path) = href.strip_prefix("file://") else { continue };
        let path = PathBuf::from(percent_decode(path));
        if path.is_file() {
            items.push((parse_iso8601(&modified).unwrap_or(0), path));
        }
    }
    items.sort_by(|a, b| b.0.cmp(&a.0));
    items.dedup_by(|a, b| a.1 == b.1);

    items
        .into_iter()
        .take(limit)
        .filter_map(|(_, path)| {
            let dir = path.parent()?;
            let name = path.file_name()?.to_string_lossy().into_owned();
            entry::stat(dir, &name, special)
        })
        .collect()
}

fn attribute(tag: &str, name: &str) -> Option<String> {
    let start = tag.find(&format!("{}=\"", name))? + name.len() + 2;
    let end = tag[start..].find('"')? + start;
    Some(tag[start..end].replace("&amp;", "&").replace("&apos;", "'").replace("&quot;", "\""))
}

fn percent_decode(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(value) = u8::from_str_radix(&text[i + 1..i + 3], 16) {
                out.push(value);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

/// "2026-10-19T12:34:56.123456Z" -> milisekundy od 1970 (UTC)
fn parse_iso8601(text: &str) -> Option<i64> {
    let (date, time) = text.split_once('T')?;
    let mut d = date.split('-').map(|p| p.parse::<i64>());
    let (year, month, day) = (d.next()?.ok()?, d.next()?.ok()?, d.next()?.ok()?);
    let time = time.trim_end_matches('Z');
    let mut t = time.split(':');
    let hour: i64 = t.next()?.parse().ok()?;
    let minute: i64 = t.next()?.parse().ok()?;
    let seconds: f64 = t.next()?.parse().ok()?;
    // Liczba dni od 1970-01-01 (algorytm "days from civil")
    let y = if month <= 2 { year - 1 } else { year };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let mp = (month + 9) % 12;
    let doy = (153 * mp + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let days = era * 146097 + doe - 719468;
    Some(((days * 86400 + hour * 3600 + minute * 60) as f64 * 1000.0 + seconds * 1000.0) as i64)
}
