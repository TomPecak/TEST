//! Usługa kalkulatora (backend) w Ruście.
//!
//! Nasłuchuje na gnieździe Unix i obsługuje kontrakt ../contract/calculator.capnp przez Cap'n Proto
//! RPC. Wszystko działa w jednym wątku (obiekty Cap'n Proto nie są Send); liczenie trwa mikrosekundy,
//! więc nie potrzeba puli wątków. Gniazdo: $TEST_IPC_CALCULATOR_SOCKET albo
//! $XDG_RUNTIME_DIR/test-ipc-calculator.sock.

pub mod calculator_capnp {
    include!(concat!(env!("OUT_DIR"), "/calculator_capnp.rs"));
}

mod engine;
mod expr;
mod number;
mod store;

use std::cell::RefCell;
use std::path::PathBuf;
use std::rc::{Rc, Weak};

use capnp_rpc::{RpcSystem, rpc_twoparty_capnp, twoparty};
use futures::AsyncReadExt;
use tokio_util::compat::TokioAsyncReadCompatExt;

use calculator_capnp::{
    calculator_service, display_state, history_listener, memory_listener, session, subscription,
};
use engine::{Engine, Key, Op, Unary, View, format_parts};
use number::{Num, NumberFormat};
use store::{MemoryAction, Store};

// --- Wspólny stan: historia, pamięć i ich słuchacze ---

struct Listener<C> {
    id: u64,
    format: NumberFormat,
    client: C,
}

struct Shared {
    store: Store,
    history_listeners: Vec<Listener<history_listener::Client>>,
    memory_listeners: Vec<Listener<memory_listener::Client>>,
    next_listener: u64,
}

type SharedRef = Rc<RefCell<Shared>>;

fn send_history(listener: &Listener<history_listener::Client>, store: &Store) {
    let mut request = listener.client.changed_request();
    let mut items = request.get().init_items(store.history().len() as u32);
    for (i, entry) in store.history().iter().enumerate() {
        let mut item = items.reborrow().get(i as u32);
        item.set_id(entry.id);
        item.set_expression(format_parts(&entry.calculation.parts, &listener.format));
        item.set_result(entry.calculation.result.format(&listener.format));
    }
    tokio::task::spawn_local(async move {
        let _ = request.send().promise.await;
    });
}

fn send_memory(listener: &Listener<memory_listener::Client>, store: &Store) {
    let mut request = listener.client.changed_request();
    let mut items = request.get().init_items(store.memory().len() as u32);
    for (i, value) in store.memory().iter().enumerate() {
        items.reborrow().get(i as u32).set_value(value.format(&listener.format));
    }
    tokio::task::spawn_local(async move {
        let _ = request.send().promise.await;
    });
}

fn notify_history(shared: &Shared) {
    for listener in &shared.history_listeners {
        send_history(listener, &shared.store);
    }
}

fn notify_memory(shared: &Shared) {
    for listener in &shared.memory_listeners {
        send_memory(listener, &shared.store);
    }
}

/// Porzucenie uchwytu przez klienta usuwa słuchacza
struct SubscriptionImpl {
    shared: Weak<RefCell<Shared>>,
    id: u64,
}

impl Drop for SubscriptionImpl {
    fn drop(&mut self) {
        if let Some(shared) = self.shared.upgrade() {
            let mut shared = shared.borrow_mut();
            shared.history_listeners.retain(|l| l.id != self.id);
            shared.memory_listeners.retain(|l| l.id != self.id);
        }
    }
}

impl subscription::Server for SubscriptionImpl {}

// --- Konwersje z kontraktu ---

fn read_format(reader: calculator_capnp::number_format::Reader) -> Result<NumberFormat, capnp::Error> {
    Ok(NumberFormat {
        decimal: reader.get_decimal()?.to_string()?,
        group: reader.get_group()?.to_string()?,
    })
}

fn engine_key(key: calculator_capnp::Key) -> Option<Key> {
    use calculator_capnp::Key as K;
    Some(match key {
        K::Digit0 => Key::Digit(0),
        K::Digit1 => Key::Digit(1),
        K::Digit2 => Key::Digit(2),
        K::Digit3 => Key::Digit(3),
        K::Digit4 => Key::Digit(4),
        K::Digit5 => Key::Digit(5),
        K::Digit6 => Key::Digit(6),
        K::Digit7 => Key::Digit(7),
        K::Digit8 => Key::Digit(8),
        K::Digit9 => Key::Digit(9),
        K::Decimal => Key::Decimal,
        K::Add => Key::Op(Op::Add),
        K::Subtract => Key::Op(Op::Sub),
        K::Multiply => Key::Op(Op::Mul),
        K::Divide => Key::Op(Op::Div),
        K::Equals => Key::Equals,
        K::Percent => Key::Percent,
        K::SquareRoot => Key::Unary(Unary::Sqrt),
        K::Square => Key::Unary(Unary::Square),
        K::Reciprocal => Key::Unary(Unary::Reciprocal),
        K::Negate => Key::Unary(Unary::Negate),
        K::ClearEntry => Key::ClearEntry,
        K::Clear => Key::Clear,
        K::Backspace => Key::Backspace,
        _ => return None, // klawisze pamięci obsługuje sesja
    })
}

fn write_state(mut builder: display_state::Builder, view: &View) {
    builder.set_display(&view.display);
    builder.set_expression(&view.expression);
    builder.set_error(view.error);
    builder.set_value(&view.value);
}

// --- Sesja: jeden kalkulator na ekranie ---

struct SessionImpl {
    engine: RefCell<Engine>,
    format: NumberFormat,
    shared: SharedRef,
}

impl SessionImpl {
    fn press_key(&self, key: calculator_capnp::Key) -> View {
        use calculator_capnp::Key as K;
        let mut engine = self.engine.borrow_mut();
        let mut shared = self.shared.borrow_mut();
        match key {
            K::MemoryStore | K::MemoryAdd | K::MemorySubtract if !engine.has_error() => {
                let value = engine.current_value();
                let changed = match key {
                    K::MemoryStore => {
                        shared.store.memory_store(value);
                        true
                    }
                    _ => shared.store.memory_add_top(&value, key == K::MemorySubtract).is_ok(),
                };
                engine.commit();
                if changed {
                    notify_memory(&shared);
                }
            }
            K::MemoryRecall => {
                if let Some(value) = shared.store.memory_top() {
                    engine.set_value(value);
                }
            }
            K::MemoryClear => {
                shared.store.memory_clear();
                notify_memory(&shared);
            }
            K::MemoryStore | K::MemoryAdd | K::MemorySubtract => {}
            other => {
                if let Some(calculation) = engine_key(other).and_then(|k| engine.press(k)) {
                    shared.store.add_history(calculation);
                    notify_history(&shared);
                }
            }
        }
        engine.view(&self.format)
    }
}

impl session::Server for SessionImpl {
    async fn press(self: Rc<Self>, params: session::PressParams, mut results: session::PressResults) -> Result<(), capnp::Error> {
        let key = params.get()?.get_key().map_err(|_| capnp::Error::failed("unknown key".into()))?;
        let view = self.press_key(key);
        write_state(results.get().init_state(), &view);
        Ok(())
    }

    async fn state(self: Rc<Self>, _: session::StateParams, mut results: session::StateResults) -> Result<(), capnp::Error> {
        let view = self.engine.borrow().view(&self.format);
        write_state(results.get().init_state(), &view);
        Ok(())
    }

    async fn recall_history(
        self: Rc<Self>,
        params: session::RecallHistoryParams,
        mut results: session::RecallHistoryResults,
    ) -> Result<(), capnp::Error> {
        let id = params.get()?.get_id();
        let calculation = self.shared.borrow().store.find_history(id).cloned();
        let mut engine = self.engine.borrow_mut();
        if let Some(calculation) = calculation {
            engine.show_calculation(&calculation);
        }
        write_state(results.get().init_state(), &engine.view(&self.format));
        Ok(())
    }

    async fn memory_item(
        self: Rc<Self>,
        params: session::MemoryItemParams,
        mut results: session::MemoryItemResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let index = params.get_index() as usize;
        let action = match params.get_action().map_err(|_| capnp::Error::failed("unknown action".into()))? {
            calculator_capnp::MemoryAction::Recall => MemoryAction::Recall,
            calculator_capnp::MemoryAction::Add => MemoryAction::Add,
            calculator_capnp::MemoryAction::Subtract => MemoryAction::Subtract,
            calculator_capnp::MemoryAction::Clear => MemoryAction::Clear,
        };
        let recall = matches!(action, MemoryAction::Recall);
        {
            let mut engine = self.engine.borrow_mut();
            let mut shared = self.shared.borrow_mut();
            let value = engine.current_value();
            if let Ok(recalled) = shared.store.memory_item(index, action, &value) {
                match recalled {
                    Some(value) => engine.set_value(value),
                    None if !recall => {
                        engine.commit();
                        notify_memory(&shared);
                    }
                    None => {}
                }
            }
        }
        write_state(results.get().init_state(), &self.engine.borrow().view(&self.format));
        Ok(())
    }

    async fn paste(self: Rc<Self>, params: session::PasteParams, mut results: session::PasteResults) -> Result<(), capnp::Error> {
        let text = params.get()?.get_text()?.to_string()?;
        let value = Num::parse_localized(&text, &self.format);
        let mut engine = self.engine.borrow_mut();
        if let Some(value) = value.clone() {
            engine.set_value(value);
        }
        let mut builder = results.get();
        builder.set_ok(value.is_some());
        write_state(builder.init_state(), &engine.view(&self.format));
        Ok(())
    }
}

// --- Usługa ---

struct CalculatorServiceImpl {
    shared: SharedRef,
}

impl CalculatorServiceImpl {
    fn subscribe(&self) -> (u64, subscription::Client) {
        let id = {
            let mut shared = self.shared.borrow_mut();
            shared.next_listener += 1;
            shared.next_listener
        };
        let handle = capnp_rpc::new_client(SubscriptionImpl { shared: Rc::downgrade(&self.shared), id });
        (id, handle)
    }
}

impl calculator_service::Server for CalculatorServiceImpl {
    async fn open_session(
        self: Rc<Self>,
        params: calculator_service::OpenSessionParams,
        mut results: calculator_service::OpenSessionResults,
    ) -> Result<(), capnp::Error> {
        let format = read_format(params.get()?.get_format()?)?;
        let session = SessionImpl { engine: RefCell::new(Engine::new()), format, shared: self.shared.clone() };
        results.get().set_session(capnp_rpc::new_client(session));
        Ok(())
    }

    async fn watch_history(
        self: Rc<Self>,
        params: calculator_service::WatchHistoryParams,
        mut results: calculator_service::WatchHistoryResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let format = read_format(params.get_format()?)?;
        let client = params.get_listener()?;
        let (id, handle) = self.subscribe();
        let mut shared = self.shared.borrow_mut();
        let listener = Listener { id, format, client };
        send_history(&listener, &shared.store); // od razu obecna lista
        shared.history_listeners.push(listener);
        results.get().set_subscription(handle);
        Ok(())
    }

    async fn watch_memory(
        self: Rc<Self>,
        params: calculator_service::WatchMemoryParams,
        mut results: calculator_service::WatchMemoryResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let format = read_format(params.get_format()?)?;
        let client = params.get_listener()?;
        let (id, handle) = self.subscribe();
        let mut shared = self.shared.borrow_mut();
        let listener = Listener { id, format, client };
        send_memory(&listener, &shared.store);
        shared.memory_listeners.push(listener);
        results.get().set_subscription(handle);
        Ok(())
    }

    async fn clear_history(
        self: Rc<Self>,
        _: calculator_service::ClearHistoryParams,
        _: calculator_service::ClearHistoryResults,
    ) -> Result<(), capnp::Error> {
        let mut shared = self.shared.borrow_mut();
        shared.store.clear_history();
        notify_history(&shared);
        Ok(())
    }

    async fn clear_memory(
        self: Rc<Self>,
        _: calculator_service::ClearMemoryParams,
        _: calculator_service::ClearMemoryResults,
    ) -> Result<(), capnp::Error> {
        let mut shared = self.shared.borrow_mut();
        shared.store.memory_clear();
        notify_memory(&shared);
        Ok(())
    }

    async fn evaluate(
        self: Rc<Self>,
        params: calculator_service::EvaluateParams,
        mut results: calculator_service::EvaluateResults,
    ) -> Result<(), capnp::Error> {
        let params = params.get()?;
        let text = params.get_expression()?.to_string()?;
        let format = read_format(params.get_format()?)?;
        let mut builder = results.get();
        match expr::evaluate(&text, &format) {
            Ok(value) => {
                builder.set_ok(true);
                builder.set_result(value.format(&format));
            }
            Err(error) => {
                builder.set_ok(false);
                builder.set_message(error.message());
            }
        }
        Ok(())
    }
}

fn socket_path() -> PathBuf {
    if let Ok(path) = std::env::var("TEST_IPC_CALCULATOR_SOCKET") {
        return PathBuf::from(path);
    }
    let runtime = std::env::var("XDG_RUNTIME_DIR").unwrap_or_else(|_| "/tmp".into());
    PathBuf::from(runtime).join("test-ipc-calculator.sock")
}

#[tokio::main(flavor = "current_thread")]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let path = socket_path();
    let _ = std::fs::remove_file(&path);
    let listener = tokio::net::UnixListener::bind(&path)?;
    // Gniazdo usuwamy przy wyjściu tylko wtedy, gdy to nadal nasze (nie nowej instancji usługi)
    let socket_inode = std::os::unix::fs::MetadataExt::ino(&std::fs::metadata(&path)?);
    println!("calculator-service {} nasłuchuje na {}", env!("CARGO_PKG_VERSION"), path.display());

    let state_file = dirs::data_dir().map(|dir| dir.join("test-ipc-calculator").join("state.json"));
    let shared = Rc::new(RefCell::new(Shared {
        store: Store::load(state_file),
        history_listeners: Vec::new(),
        memory_listeners: Vec::new(),
        next_listener: 0,
    }));
    let service: calculator_service::Client = capnp_rpc::new_client(CalculatorServiceImpl { shared });

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
