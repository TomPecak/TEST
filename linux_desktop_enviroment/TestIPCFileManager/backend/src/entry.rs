//! Opis pojedynczego wpisu folderu: metadane z systemu plików, typ MIME, ikona i nazwa typu.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

use crate::files_capnp::entry;

#[derive(Clone, Debug)]
pub struct EntryInfo {
    pub name: String,
    pub is_dir: bool,
    pub is_symlink: bool,
    pub is_hidden: bool,
    pub size: u64,
    pub modified: i64,
    pub mime_type: String,
    pub icon_name: String,
    pub type_name: String,
    pub location: String,
}

/// Foldery ze specjalnymi ikonami (folder domowy i foldery XDG).
pub struct SpecialFolders {
    icons: HashMap<PathBuf, &'static str>,
}

impl SpecialFolders {
    pub fn new() -> Self {
        let mut icons = HashMap::new();
        let mut add = |path: Option<PathBuf>, icon: &'static str| {
            if let Some(path) = path {
                icons.insert(path, icon);
            }
        };
        add(dirs::home_dir(), "user-home");
        add(dirs::desktop_dir(), "user-desktop");
        add(dirs::document_dir(), "folder-documents");
        add(dirs::download_dir(), "folder-download");
        add(dirs::audio_dir(), "folder-music");
        add(dirs::picture_dir(), "folder-pictures");
        add(dirs::video_dir(), "folder-videos");
        add(dirs::template_dir(), "folder-templates");
        add(dirs::public_dir(), "folder-publicshare");
        add(dirs::home_dir().map(|h| h.join(".local/share/Trash/files")), "user-trash");
        Self { icons }
    }

    pub fn icon(&self, path: &Path) -> Option<&'static str> {
        self.icons.get(path).copied()
    }
}

/// Czyta metadane wpisu `name` w folderze `dir`. Zwraca None, gdy wpis zniknął w międzyczasie.
pub fn stat(dir: &Path, name: &str, special: &SpecialFolders) -> Option<EntryInfo> {
    let path = dir.join(name);
    let link_meta = fs::symlink_metadata(&path).ok()?;
    let is_symlink = link_meta.file_type().is_symlink();
    // Dla dowiązań liczy się cel; zerwane dowiązanie pokazujemy jako plik
    let meta = if is_symlink { fs::metadata(&path).unwrap_or(link_meta) } else { link_meta };
    let is_dir = meta.is_dir();
    let modified = meta
        .modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0);

    let (mime_type, icon_name, type_name) = if is_dir {
        let icon = special.icon(&path).unwrap_or("folder");
        ("inode/directory".to_string(), icon.to_string(), "Folder".to_string())
    } else {
        describe_file(name)
    };

    Some(EntryInfo {
        name: name.to_string(),
        is_dir,
        is_symlink,
        is_hidden: name.starts_with('.'),
        size: if is_dir { 0 } else { meta.len() },
        modified,
        mime_type,
        icon_name,
        type_name,
        location: dir.to_string_lossy().into_owned(),
    })
}

/// Typ MIME (po rozszerzeniu), nazwa ikony freedesktop i opis typu dla człowieka.
/// Ikonę zastępczą (gdy motyw nie ma tej konkretnej) wybiera frontend, bo to on zna motyw ikon.
fn describe_file(name: &str) -> (String, String, String) {
    let mime = mime_guess::from_path(name).first_raw().unwrap_or("application/octet-stream");
    let extension = Path::new(name)
        .extension()
        .map(|e| e.to_string_lossy().to_uppercase())
        .unwrap_or_default();

    let known = match mime {
        "application/pdf" => Some("PDF document"),
        "application/zip" => Some("ZIP archive"),
        "application/x-tar" | "application/gzip" | "application/x-bzip2" | "application/x-xz"
        | "application/x-7z-compressed" | "application/vnd.rar" => Some("Archive"),
        "application/vnd.oasis.opendocument.text" => Some("Text document"),
        "application/vnd.oasis.opendocument.spreadsheet" => Some("Spreadsheet"),
        "application/vnd.oasis.opendocument.presentation" => Some("Presentation"),
        "application/msword" | "application/vnd.openxmlformats-officedocument.wordprocessingml.document" => {
            Some("Word document")
        }
        "application/vnd.ms-excel" | "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" => {
            Some("Excel spreadsheet")
        }
        "application/json" => Some("JSON file"),
        "application/x-sh" => Some("Shell script"),
        "text/markdown" | "text/x-markdown" => Some("Markdown document"),
        "text/plain" => Some("Text file"),
        "text/html" => Some("HTML document"),
        "text/x-python" => Some("Python script"),
        "application/x-iso9660-image" => Some("Disc image"),
        _ => None,
    };
    let type_name = match known {
        Some(name) => name.to_string(),
        None => {
            let kind = match mime.split('/').next().unwrap_or("") {
                "image" => "image",
                "audio" => "audio",
                "video" => "video",
                "text" => "text file",
                "font" => "font",
                _ => "file",
            };
            if extension.is_empty() { format!("{}{}", kind[..1].to_uppercase(), &kind[1..]) } else { format!("{} {}", extension, kind) }
        }
    };
    (mime.to_string(), mime.replace('/', "-"), type_name)
}

pub fn write(mut builder: entry::Builder, entry: &EntryInfo) {
    builder.set_name(entry.name.as_str());
    builder.set_is_dir(entry.is_dir);
    builder.set_is_symlink(entry.is_symlink);
    builder.set_is_hidden(entry.is_hidden);
    builder.set_size(entry.size);
    builder.set_modified(entry.modified);
    builder.set_mime_type(entry.mime_type.as_str());
    builder.set_icon_name(entry.icon_name.as_str());
    builder.set_type_name(entry.type_name.as_str());
    builder.set_location(entry.location.as_str());
}
