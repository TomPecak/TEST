//! Operacje na plikach: nowy folder, zmiana nazwy, kosz, kopiowanie i przenoszenie z postępem.

use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{Duration, Instant};

use tokio::sync::mpsc;

use crate::files_capnp::job_listener;
use crate::watch::Cancel;

/// Nazwa, która nie koliduje z istniejącymi: "New folder", "New folder (2)", ...
/// Dla plików numer trafia przed rozszerzenie: "photo (2).jpg".
pub fn unique_name(parent: &Path, base: &str) -> String {
    if !parent.join(base).exists() {
        return base.to_string();
    }
    let (stem, extension) = match base.rfind('.') {
        Some(dot) if dot > 0 => (&base[..dot], &base[dot..]),
        _ => (base, ""),
    };
    (2..)
        .map(|i| format!("{} ({}){}", stem, i, extension))
        .find(|candidate| !parent.join(candidate).exists())
        .unwrap()
}

pub fn create_folder(parent: &Path, base: &str) -> Result<String, String> {
    let name = unique_name(parent, base);
    fs::create_dir(parent.join(&name)).map_err(|e| e.to_string())?;
    Ok(name)
}

pub fn rename(parent: &Path, old_name: &str, new_name: &str) -> Result<(), String> {
    let new_name = new_name.trim();
    if new_name.is_empty() || new_name.contains('/') || new_name == "." || new_name == ".." {
        return Err("Invalid name".into());
    }
    if new_name == old_name {
        return Ok(());
    }
    if parent.join(new_name).exists() {
        return Err(format!("“{}” already exists in this folder", new_name));
    }
    fs::rename(parent.join(old_name), parent.join(new_name)).map_err(|e| e.to_string())
}

// --- Zadania z postępem ---

pub enum Progress {
    Step { done_bytes: u64, total_bytes: u64, done_items: u32, total_items: u32, current: String },
    Finished { ok: bool, message: String },
}

/// Uruchamia pracę blokującą i przekazuje jej postęp słuchaczowi klienta.
fn run_job<F>(listener: job_listener::Client, cancel: Arc<Cancel>, work: F)
where
    F: FnOnce(&mpsc::Sender<Progress>, &Cancel) -> Result<String, String> + Send + 'static,
{
    let (tx, mut rx) = mpsc::channel::<Progress>(16);
    {
        let cancel = cancel.clone();
        tokio::task::spawn_blocking(move || {
            let result = work(&tx, &cancel);
            let (ok, message) = match result {
                Ok(message) => (true, message),
                Err(message) => (false, message),
            };
            let _ = tx.blocking_send(Progress::Finished { ok, message });
        });
    }
    tokio::task::spawn_local(async move {
        while let Some(progress) = rx.recv().await {
            let result = match progress {
                Progress::Step { done_bytes, total_bytes, done_items, total_items, current } => {
                    let mut request = listener.progress_request();
                    let mut params = request.get();
                    params.set_done_bytes(done_bytes);
                    params.set_total_bytes(total_bytes);
                    params.set_done_items(done_items);
                    params.set_total_items(total_items);
                    params.set_current(current.as_str());
                    request.send().promise.await
                }
                Progress::Finished { ok, message } => {
                    let mut request = listener.finished_request();
                    request.get().set_ok(ok);
                    request.get().set_message(message.as_str());
                    let _ = request.send().promise.await;
                    return;
                }
            };
            if result.is_err() {
                // Klient zniknął: zadanie kończy się samo przy następnym sprawdzeniu anulowania
                cancel.cancel();
            }
        }
    });
}

pub fn trash(paths: Vec<PathBuf>, listener: job_listener::Client, cancel: Arc<Cancel>) {
    run_job(listener, cancel, move |tx, cancel| {
        let total = paths.len() as u32;
        for (i, path) in paths.iter().enumerate() {
            if cancel.is_cancelled() {
                return Err("Cancelled".into());
            }
            let _ = tx.blocking_send(Progress::Step {
                done_bytes: 0,
                total_bytes: 0,
                done_items: i as u32,
                total_items: total,
                current: path.file_name().unwrap_or_default().to_string_lossy().into_owned(),
            });
            trash::delete(path).map_err(|e| format!("{}: {}", path.display(), e))?;
        }
        Ok(format!("{} item(s) moved to the trash", total))
    });
}

/// Pliki i rozmiary do skopiowania (foldery rekurencyjnie, dowiązania jako dowiązania).
fn measure(path: &Path, items: &mut u32, bytes: &mut u64) {
    let Ok(meta) = fs::symlink_metadata(path) else { return };
    *items += 1;
    if meta.is_dir() {
        if let Ok(reader) = fs::read_dir(path) {
            for item in reader.flatten() {
                measure(&item.path(), items, bytes);
            }
        }
    } else if meta.is_file() {
        *bytes += meta.len();
    }
}

struct Copier<'a> {
    tx: &'a mpsc::Sender<Progress>,
    cancel: &'a Cancel,
    total_bytes: u64,
    total_items: u32,
    done_bytes: u64,
    done_items: u32,
    last_report: Instant,
}

impl Copier<'_> {
    fn report(&mut self, current: &Path, force: bool) {
        if force || self.last_report.elapsed() > Duration::from_millis(100) {
            self.last_report = Instant::now();
            let _ = self.tx.blocking_send(Progress::Step {
                done_bytes: self.done_bytes,
                total_bytes: self.total_bytes,
                done_items: self.done_items,
                total_items: self.total_items,
                current: current.file_name().unwrap_or_default().to_string_lossy().into_owned(),
            });
        }
    }

    fn copy(&mut self, source: &Path, target: &Path) -> Result<(), String> {
        if self.cancel.is_cancelled() {
            return Err("Cancelled".into());
        }
        let meta = fs::symlink_metadata(source).map_err(|e| format!("{}: {}", source.display(), e))?;
        if meta.file_type().is_symlink() {
            let link = fs::read_link(source).map_err(|e| e.to_string())?;
            std::os::unix::fs::symlink(link, target).map_err(|e| format!("{}: {}", target.display(), e))?;
        } else if meta.is_dir() {
            fs::create_dir(target).map_err(|e| format!("{}: {}", target.display(), e))?;
            for item in fs::read_dir(source).map_err(|e| e.to_string())?.flatten() {
                self.copy(&item.path(), &target.join(item.file_name()))?;
            }
            let _ = fs::set_permissions(target, meta.permissions());
        } else {
            let mut input = fs::File::open(source).map_err(|e| format!("{}: {}", source.display(), e))?;
            let mut output = fs::File::create(target).map_err(|e| format!("{}: {}", target.display(), e))?;
            let mut buffer = vec![0u8; 1 << 20];
            loop {
                if self.cancel.is_cancelled() {
                    drop(output);
                    let _ = fs::remove_file(target);
                    return Err("Cancelled".into());
                }
                let read = input.read(&mut buffer).map_err(|e| e.to_string())?;
                if read == 0 {
                    break;
                }
                output.write_all(&buffer[..read]).map_err(|e| format!("{}: {}", target.display(), e))?;
                self.done_bytes += read as u64;
                self.report(source, false);
            }
            let _ = fs::set_permissions(target, meta.permissions());
        }
        self.done_items += 1;
        self.report(source, false);
        Ok(())
    }
}

pub fn transfer(sources: Vec<PathBuf>, destination: PathBuf, is_move: bool, listener: job_listener::Client, cancel: Arc<Cancel>) {
    run_job(listener, cancel, move |tx, cancel| {
        if !destination.is_dir() {
            return Err(format!("“{}” is not a folder", destination.display()));
        }
        let mut total_items = 0;
        let mut total_bytes = 0;
        for source in &sources {
            measure(source, &mut total_items, &mut total_bytes);
        }
        let mut copier = Copier { tx, cancel, total_bytes, total_items, done_bytes: 0, done_items: 0, last_report: Instant::now() };

        for source in &sources {
            let Some(name) = source.file_name() else { continue };
            let name = name.to_string_lossy();
            if destination.starts_with(source) {
                return Err(format!("Can't copy “{}” into itself", name));
            }
            if is_move && source.parent() == Some(destination.as_path()) {
                continue; // przeniesienie w to samo miejsce
            }
            let target = destination.join(unique_name(&destination, &name));
            copier.report(source, true);

            // Przeniesienie w obrębie jednego systemu plików to tylko zmiana nazwy
            if is_move && fs::rename(source, &target).is_ok() {
                let (mut items, mut bytes) = (0, 0);
                measure(&target, &mut items, &mut bytes);
                copier.done_items += items;
                copier.done_bytes += bytes;
                continue;
            }
            copier.copy(source, &target)?;
            if is_move {
                let removed = if source.is_dir() { fs::remove_dir_all(source) } else { fs::remove_file(source) };
                removed.map_err(|e| format!("{}: {}", source.display(), e))?;
            }
        }
        copier.report(&destination, true);
        let verb = if is_move { "moved" } else { "copied" };
        Ok(format!("{} item(s) {}", sources.len(), verb))
    });
}
