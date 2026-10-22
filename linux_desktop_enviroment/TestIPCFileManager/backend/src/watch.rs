//! Zawartość folderów (migawka partiami + zmiany na żywo przez inotify) i wyszukiwanie.
//!
//! Praca z dyskiem idzie w wątkach blokujących (spawn_blocking), a wysyłanie do klienta w zadaniu
//! lokalnym (obiekty RPC nie są Send). Zadanie kończy się, gdy klient porzuci uchwyt subskrypcji.

use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use notify::{RecursiveMode, Watcher};
use tokio::sync::{Notify, mpsc};

use crate::entry::{self, EntryInfo, SpecialFolders};
use crate::files_capnp::{directory_listener, subscription};

/// Pierwsza partia jest mała, żeby folder pojawił się w pierwszej klatce; kolejne większe.
const FIRST_BATCH: usize = 100;
const BATCH: usize = 1000;
/// Zmiany z inotify zbieramy przez chwilę, żeby np. kopiowanie 500 plików nie dało 500 wiadomości.
const DEBOUNCE: Duration = Duration::from_millis(40);
const SEARCH_LIMIT: usize = 5000;
const SEARCH_MAX_DEPTH: usize = 16;

/// Wspólny sygnał anulowania dla zadania lokalnego i wątku blokującego.
pub struct Cancel {
    flag: AtomicBool,
    notify: Notify,
}

impl Cancel {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { flag: AtomicBool::new(false), notify: Notify::new() })
    }

    pub fn cancel(&self) {
        self.flag.store(true, Ordering::Relaxed);
        self.notify.notify_one();
    }

    pub fn is_cancelled(&self) -> bool {
        self.flag.load(Ordering::Relaxed)
    }

    pub async fn cancelled(&self) {
        while !self.is_cancelled() {
            self.notify.notified().await;
        }
    }
}

/// Uchwyt subskrypcji zwracany klientowi. Porzucenie (drop) albo cancel() kończy pracę serwera.
pub struct SubscriptionImpl {
    cancel: Arc<Cancel>,
}

impl SubscriptionImpl {
    pub fn new(cancel: Arc<Cancel>) -> Self {
        Self { cancel }
    }
}

impl Drop for SubscriptionImpl {
    fn drop(&mut self) {
        self.cancel.cancel();
    }
}

impl subscription::Server for SubscriptionImpl {
    async fn cancel(
        self: Rc<Self>,
        _: subscription::CancelParams,
        _: subscription::CancelResults,
    ) -> Result<(), capnp::Error> {
        self.cancel.cancel();
        Ok(())
    }
}

// --- Wysyłanie do słuchacza ---

enum Kind {
    Batch,
    Added,
    Changed,
}

async fn send_entries(listener: &directory_listener::Client, kind: Kind, entries: &[EntryInfo]) -> Result<(), capnp::Error> {
    macro_rules! send {
        ($request:expr) => {{
            let mut request = $request;
            let mut list = request.get().init_entries(entries.len() as u32);
            for (i, e) in entries.iter().enumerate() {
                entry::write(list.reborrow().get(i as u32), e);
            }
            request.send().promise.await?;
        }};
    }
    match kind {
        Kind::Batch => send!(listener.batch_request()),
        Kind::Added => send!(listener.added_request()),
        Kind::Changed => send!(listener.changed_request()),
    }
    Ok(())
}

async fn send_removed(listener: &directory_listener::Client, names: &[String]) -> Result<(), capnp::Error> {
    let mut request = listener.removed_request();
    let mut list = request.get().init_names(names.len() as u32);
    for (i, name) in names.iter().enumerate() {
        list.set(i as u32, name.as_str());
    }
    request.send().promise.await?;
    Ok(())
}

async fn send_done(listener: &directory_listener::Client, count: u32) -> Result<(), capnp::Error> {
    let mut request = listener.snapshot_done_request();
    request.get().set_count(count);
    request.send().promise.await?;
    Ok(())
}

async fn send_failed(listener: &directory_listener::Client, message: &str) {
    let mut request = listener.failed_request();
    request.get().set_message(message);
    let _ = request.send().promise.await;
}

// --- Obserwowanie folderu ---

pub fn watch_directory(path: PathBuf, listener: directory_listener::Client, special: Arc<SpecialFolders>, cancel: Arc<Cancel>) {
    tokio::task::spawn_local(async move {
        if let Err(error) = run_watch(&path, &listener, special, &cancel).await {
            // Najczęściej: klient się rozłączył albo porzucił słuchacza
            if !cancel.is_cancelled() {
                eprintln!("watch {}: {}", path.display(), error);
            }
        }
    });
}

async fn run_watch(path: &Path, listener: &directory_listener::Client, special: Arc<SpecialFolders>, cancel: &Arc<Cancel>) -> Result<(), capnp::Error> {
    if !path.is_dir() {
        send_failed(listener, &format!("Folder “{}” does not exist or is not a folder", path.display())).await;
        return Ok(());
    }

    // Obserwator startuje przed migawką, żeby nie zgubić zmian z jej trakcie;
    // podwójne zgłoszenia uzgadniamy ze zbiorem znanych nazw.
    let (event_tx, mut event_rx) = mpsc::unbounded_channel::<Vec<String>>();
    let watched = path.to_path_buf();
    let mut watcher = match notify::recommended_watcher(move |result: notify::Result<notify::Event>| {
        if let Ok(event) = result {
            let names: Vec<String> = event
                .paths
                .iter()
                .filter(|p| p.parent() == Some(watched.as_path()) || *p == &watched)
                .map(|p| if *p == watched { String::new() } else { p.file_name().unwrap_or_default().to_string_lossy().into_owned() })
                .collect();
            if !names.is_empty() {
                let _ = event_tx.send(names);
            }
        }
    }) {
        Ok(watcher) => Some(watcher),
        Err(error) => {
            eprintln!("inotify niedostępne dla {}: {}", path.display(), error);
            None
        }
    };
    if let Some(watcher) = watcher.as_mut() {
        let _ = watcher.watch(path, RecursiveMode::NonRecursive);
    }

    // --- Migawka partiami ---
    let (batch_tx, mut batch_rx) = mpsc::channel::<Result<Vec<EntryInfo>, String>>(4);
    {
        let path = path.to_path_buf();
        let special = special.clone();
        let cancel = cancel.clone();
        tokio::task::spawn_blocking(move || {
            let reader = match fs::read_dir(&path) {
                Ok(reader) => reader,
                Err(error) => {
                    let _ = batch_tx.blocking_send(Err(format!("Can't open “{}”: {}", path.display(), error)));
                    return;
                }
            };
            let mut batch = Vec::with_capacity(FIRST_BATCH);
            let mut limit = FIRST_BATCH;
            for item in reader.flatten() {
                if cancel.is_cancelled() {
                    return;
                }
                let name = item.file_name().to_string_lossy().into_owned();
                if let Some(info) = entry::stat(&path, &name, &special) {
                    batch.push(info);
                }
                if batch.len() >= limit {
                    if batch_tx.blocking_send(Ok(std::mem::take(&mut batch))).is_err() {
                        return;
                    }
                    limit = BATCH;
                }
            }
            if !batch.is_empty() {
                let _ = batch_tx.blocking_send(Ok(batch));
            }
        });
    }

    let mut known: HashSet<String> = HashSet::new();
    while let Some(message) = batch_rx.recv().await {
        if cancel.is_cancelled() {
            return Ok(());
        }
        match message {
            Ok(batch) => {
                known.extend(batch.iter().map(|e| e.name.clone()));
                send_entries(listener, Kind::Batch, &batch).await?;
            }
            Err(message) => {
                send_failed(listener, &message).await;
                return Ok(());
            }
        }
    }
    send_done(listener, known.len() as u32).await?;

    // --- Zmiany na żywo ---
    loop {
        let mut pending: HashSet<String> = tokio::select! {
            _ = cancel.cancelled() => return Ok(()),
            names = event_rx.recv() => match names {
                Some(names) => names.into_iter().collect(),
                None => return Ok(()),
            },
        };
        let deadline = tokio::time::sleep(DEBOUNCE);
        tokio::pin!(deadline);
        loop {
            tokio::select! {
                _ = &mut deadline => break,
                more = event_rx.recv() => match more {
                    Some(names) => pending.extend(names),
                    None => break,
                },
            }
        }

        // Pusta nazwa = zdarzenie samego folderu (np. usunięty)
        if pending.remove("") && !path.is_dir() {
            send_failed(listener, "The folder was removed").await;
            return Ok(());
        }

        let mut added = Vec::new();
        let mut changed = Vec::new();
        let mut removed = Vec::new();
        for name in pending {
            match entry::stat(path, &name, &special) {
                Some(info) => {
                    if known.insert(name) { added.push(info) } else { changed.push(info) }
                }
                None => {
                    if known.remove(&name) {
                        removed.push(name);
                    }
                }
            }
        }
        if !removed.is_empty() {
            send_removed(listener, &removed).await?;
        }
        if !added.is_empty() {
            send_entries(listener, Kind::Added, &added).await?;
        }
        if !changed.is_empty() {
            send_entries(listener, Kind::Changed, &changed).await?;
        }
    }
}

// --- Wyszukiwanie ---

pub fn search(path: PathBuf, query: String, listener: directory_listener::Client, special: Arc<SpecialFolders>, cancel: Arc<Cancel>) {
    tokio::task::spawn_local(async move {
        let (tx, mut rx) = mpsc::channel::<Vec<EntryInfo>>(4);
        {
            let cancel = cancel.clone();
            let needle = query.to_lowercase();
            tokio::task::spawn_blocking(move || {
                let mut batch = Vec::new();
                let mut found = 0usize;
                let mut last_send = Instant::now();
                let mut stack = vec![(path, 0usize)];
                while let Some((dir, depth)) = stack.pop() {
                    let Ok(reader) = fs::read_dir(&dir) else { continue };
                    for item in reader.flatten() {
                        if cancel.is_cancelled() || found >= SEARCH_LIMIT {
                            return;
                        }
                        let name = item.file_name().to_string_lossy().into_owned();
                        let Ok(file_type) = item.file_type() else { continue };
                        // Ukryte foldery i dowiązania pomijamy (szybkość, pętle dowiązań)
                        if file_type.is_dir() && !name.starts_with('.') && depth < SEARCH_MAX_DEPTH {
                            stack.push((item.path(), depth + 1));
                        }
                        if name.to_lowercase().contains(&needle) {
                            if let Some(info) = entry::stat(&dir, &name, &special) {
                                batch.push(info);
                                found += 1;
                            }
                        }
                    }
                    // Wyniki płyną partiami: co 200 trafień albo co 100 ms
                    if !batch.is_empty() && (batch.len() >= 200 || last_send.elapsed() > Duration::from_millis(100)) {
                        if tx.blocking_send(std::mem::take(&mut batch)).is_err() {
                            return;
                        }
                        last_send = Instant::now();
                    }
                }
                if !batch.is_empty() {
                    let _ = tx.blocking_send(batch);
                }
            });
        }

        let mut count = 0u32;
        while let Some(batch) = rx.recv().await {
            if cancel.is_cancelled() {
                return;
            }
            count += batch.len() as u32;
            if send_entries(&listener, Kind::Batch, &batch).await.is_err() {
                return;
            }
        }
        if !cancel.is_cancelled() {
            let _ = send_done(&listener, count).await;
        }
    });
}
