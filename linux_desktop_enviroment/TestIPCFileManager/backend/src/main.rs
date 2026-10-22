//! Usługa plików (backend menedżera plików) w Ruście.
//!
//! Nasłuchuje na gnieździe Unix i obsługuje kontrakt ../contract/files.capnp przez Cap'n Proto RPC.
//! Jeden wątek obsługuje RPC (obiekty Cap'n Proto nie są Send), praca z dyskiem idzie do puli
//! wątków blokujących. Gniazdo: $TEST_IPC_FILES_SOCKET albo $XDG_RUNTIME_DIR/test-ipc-files.sock.

pub mod files_capnp {
    include!(concat!(env!("OUT_DIR"), "/files_capnp.rs"));
}

mod entry;
mod ops;
mod system;
mod watch;

use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;

use capnp_rpc::{RpcSystem, rpc_twoparty_capnp, twoparty};
use futures::AsyncReadExt;
use tokio_util::compat::TokioAsyncReadCompatExt;

use entry::SpecialFolders;
use files_capnp::{TransferMode, file_service};
use watch::{Cancel, SubscriptionImpl};

struct FileServiceImpl {
    special: Arc<SpecialFolders>,
}

fn text_list(reader: capnp::text_list::Reader) -> Result<Vec<PathBuf>, capnp::Error> {
    reader.iter().map(|t| Ok(PathBuf::from(t?.to_str()?))).collect()
}

impl file_service::Server for FileServiceImpl {
    async fn info(
        self: Rc<Self>,
        _: file_service::InfoParams,
        mut results: file_service::InfoResults,
    ) -> Result<(), capnp::Error> {
        let home = dirs::home_dir().unwrap_or_else(|| PathBuf::from("/"));
        let mut r = results.get();
        r.set_home_path(home.to_string_lossy().as_ref());
        r.set_trash_path(home.join(".local/share/Trash/files").to_string_lossy().as_ref());
        r.set_service_version(env!("CARGO_PKG_VERSION"));
        Ok(())
    }

    async fn places(
        self: Rc<Self>,
        _: file_service::PlacesParams,
        mut results: file_service::PlacesResults,
    ) -> Result<(), capnp::Error> {
        let places = system::places();
        let mut list = results.get().init_places(places.len() as u32);
        for (i, place) in places.iter().enumerate() {
            let mut p = list.reborrow().get(i as u32);
            p.set_name(place.name.as_str());
            p.set_path(place.path.as_str());
            p.set_icon_name(place.icon_name.as_str());
        }
        Ok(())
    }

    async fn drives(
        self: Rc<Self>,
        _: file_service::DrivesParams,
        mut results: file_service::DrivesResults,
    ) -> Result<(), capnp::Error> {
        let drives = tokio::task::spawn_blocking(system::drives).await.unwrap_or_default();
        let mut list = results.get().init_drives(drives.len() as u32);
        for (i, drive) in drives.iter().enumerate() {
            let mut d = list.reborrow().get(i as u32);
            d.set_name(drive.name.as_str());
            d.set_mount_point(drive.mount_point.as_str());
            d.set_device(drive.device.as_str());
            d.set_fs_type(drive.fs_type.as_str());
            d.set_total(drive.total);
            d.set_free(drive.free);
            d.set_removable(drive.removable);
        }
        Ok(())
    }

    async fn recent(
        self: Rc<Self>,
        params: file_service::RecentParams,
        mut results: file_service::RecentResults,
    ) -> Result<(), capnp::Error> {
        let limit = params.get()?.get_limit() as usize;
        let special = self.special.clone();
        let entries = tokio::task::spawn_blocking(move || system::recent(limit, &special)).await.unwrap_or_default();
        let mut list = results.get().init_entries(entries.len() as u32);
        for (i, e) in entries.iter().enumerate() {
            entry::write(list.reborrow().get(i as u32), e);
        }
        Ok(())
    }

    async fn watch_directory(
        self: Rc<Self>,
        params: file_service::WatchDirectoryParams,
        mut results: file_service::WatchDirectoryResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let path = PathBuf::from(params.get_path()?.to_str()?);
        let listener = params.get_listener()?;
        let cancel = Cancel::new();
        results.get().set_subscription(capnp_rpc::new_client(SubscriptionImpl::new(cancel.clone())));
        watch::watch_directory(path, listener, self.special.clone(), cancel);
        Ok(())
    }

    async fn search(
        self: Rc<Self>,
        params: file_service::SearchParams,
        mut results: file_service::SearchResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let path = PathBuf::from(params.get_path()?.to_str()?);
        let query = params.get_query()?.to_str()?.to_string();
        let listener = params.get_listener()?;
        let cancel = Cancel::new();
        results.get().set_subscription(capnp_rpc::new_client(SubscriptionImpl::new(cancel.clone())));
        watch::search(path, query, listener, self.special.clone(), cancel);
        Ok(())
    }

    async fn create_folder(
        self: Rc<Self>,
        params: file_service::CreateFolderParams,
        mut results: file_service::CreateFolderResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let parent = PathBuf::from(params.get_parent()?.to_str()?);
        let base = params.get_base_name()?.to_str()?.to_string();
        let name = tokio::task::spawn_blocking(move || ops::create_folder(&parent, &base))
            .await
            .map_err(|e| capnp::Error::failed(e.to_string()))?
            .map_err(capnp::Error::failed)?;
        results.get().set_name(name.as_str());
        Ok(())
    }

    async fn rename(
        self: Rc<Self>,
        params: file_service::RenameParams,
        mut results: file_service::RenameResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let parent = PathBuf::from(params.get_parent()?.to_str()?);
        let old_name = params.get_old_name()?.to_str()?.to_string();
        let new_name = params.get_new_name()?.to_str()?.to_string();
        let result = tokio::task::spawn_blocking(move || ops::rename(&parent, &old_name, &new_name))
            .await
            .map_err(|e| capnp::Error::failed(e.to_string()))?;
        let mut r = results.get();
        r.set_ok(result.is_ok());
        r.set_message(result.err().unwrap_or_default().as_str());
        Ok(())
    }

    async fn trash(
        self: Rc<Self>,
        params: file_service::TrashParams,
        mut results: file_service::TrashResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let paths = text_list(params.get_paths()?)?;
        let cancel = Cancel::new();
        results.get().set_job(capnp_rpc::new_client(SubscriptionImpl::new(cancel.clone())));
        ops::trash(paths, params.get_listener()?, cancel);
        Ok(())
    }

    async fn transfer(
        self: Rc<Self>,
        params: file_service::TransferParams,
        mut results: file_service::TransferResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let sources = text_list(params.get_sources()?)?;
        let destination = PathBuf::from(params.get_destination()?.to_str()?);
        let is_move = params.get_mode()? == TransferMode::Move;
        let cancel = Cancel::new();
        results.get().set_job(capnp_rpc::new_client(SubscriptionImpl::new(cancel.clone())));
        ops::transfer(sources, destination, is_move, params.get_listener()?, cancel);
        Ok(())
    }
}

fn socket_path() -> PathBuf {
    if let Ok(path) = std::env::var("TEST_IPC_FILES_SOCKET") {
        return PathBuf::from(path);
    }
    let runtime = std::env::var("XDG_RUNTIME_DIR").unwrap_or_else(|_| "/tmp".into());
    PathBuf::from(runtime).join("test-ipc-files.sock")
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = socket_path();
    let _ = std::fs::remove_file(&path);
    let listener = tokio::net::UnixListener::bind(&path)?;
    // Gniazdo usuwamy przy wyjściu tylko wtedy, gdy to nadal nasze (nie nowej instancji usługi)
    let socket_inode = std::os::unix::fs::MetadataExt::ino(&std::fs::metadata(&path)?);
    println!("files-service {} nasłuchuje na {}", env!("CARGO_PKG_VERSION"), path.display());

    let service: file_service::Client = capnp_rpc::new_client(FileServiceImpl { special: Arc::new(SpecialFolders::new()) });

    let local = tokio::task::LocalSet::new();
    let accept_loop = local.run_until(async move {
        loop {
            let (stream, _) = match listener.accept().await {
                Ok(connection) => connection,
                Err(error) => {
                    eprintln!("accept: {}", error);
                    continue;
                }
            };
            let (reader, writer) = stream.compat().split();
            let network = twoparty::VatNetwork::new(
                futures::io::BufReader::new(reader),
                futures::io::BufWriter::new(writer),
                rpc_twoparty_capnp::Side::Server,
                Default::default(),
            );
            let rpc_system = RpcSystem::new(Box::new(network), Some(service.clone().client));
            tokio::task::spawn_local(async move {
                if let Err(error) = rpc_system.await {
                    eprintln!("połączenie zakończone: {}", error);
                }
            });
        }
    });

    let mut terminate = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())?;
    tokio::select! {
        _ = accept_loop => {}
        _ = tokio::signal::ctrl_c() => {}
        _ = terminate.recv() => {}
    }
    if std::fs::metadata(&path).map(|m| std::os::unix::fs::MetadataExt::ino(&m)).ok() == Some(socket_inode) {
        let _ = std::fs::remove_file(&path);
    }
    Ok(())
}
